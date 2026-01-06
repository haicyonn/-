#include "app_audio.h"

#include <string.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

#define AUDIO_FS_HZ        AUDIO_SAMPLE_RATE_HZ
#define I2S_DMA_WORDS      512U
#define REC_MAX_SAMPLES    (AUDIO_FS_HZ * 5U)
#define AUDIO_SAMPLES_PER_MS       (AUDIO_FS_HZ / 1000U)
#define AUDIO_RX_CHANNEL_LEFT      0U
#define AUDIO_RX_CHANNEL_RIGHT     1U
#define AUDIO_RX_CHANNEL_SELECT    AUDIO_RX_CHANNEL_LEFT
#define VAD_SILENCE_PEAK           200
#define VAD_MIN_SPEECH_MS          200U
#define VAD_SILENCE_MS             800U
#define VAD_MIN_SPEECH_SAMPLES     (AUDIO_SAMPLES_PER_MS * VAD_MIN_SPEECH_MS)
#define VAD_SILENCE_SAMPLES        (AUDIO_SAMPLES_PER_MS * VAD_SILENCE_MS)
#define HPF_A_Q15                  32112
#define LPF_ENABLE                 1U
/* First-order low-pass: y[n]=y[n-1]+a*(x-y[n-1]); a=0.5 (~2.5-3kHz @16k). */
#define LPF_ALPHA_Q15              16384
#define NOISE_CAL_MS               400U
#define NOISE_CAL_SAMPLES          (AUDIO_SAMPLES_PER_MS * NOISE_CAL_MS)
#define NOISE_FLOOR_MIN            200U
#define NOISE_FLOOR_MAX            8000U
#define NOISE_FLOOR_ALPHA_SHIFT    5U
#define NOISE_GATE_OPEN_MULT       2U
#define NOISE_GATE_CLOSE_MULT      1U
#define NOISE_ATTEN_Q15            16384
#define AUDIO_STREAM_BUFFER_BYTES   (64U * 1024U)
#define AUDIO_STREAM_TRIGGER_BYTES  1U

#define I2S_DMA_HALF_WORDS          (I2S_DMA_WORDS / 2U)
#define I2S_DMA_MONO_SAMPLES        (I2S_DMA_HALF_WORDS / 2U)

static I2S_HandleTypeDef *s_hi2s = NULL;

static int16_t s_i2s_tx[I2S_DMA_WORDS];
static int16_t s_i2s_rx[I2S_DMA_WORDS];

static volatile bool s_i2s_started = false;
static volatile bool s_recording = false;
static volatile bool s_playing = false;

static int16_t s_rec_buf[REC_MAX_SAMPLES] __attribute__((section(".sdram_bss")));
static volatile uint32_t s_rec_len = 0;
static volatile uint32_t s_play_idx = 0;
static volatile int16_t s_last_peak = 0;

static StaticStreamBuffer_t s_stream_cb;
static uint8_t s_stream_storage[AUDIO_STREAM_BUFFER_BYTES] __attribute__((section(".sdram_bss")));
static StreamBufferHandle_t s_stream_handle = NULL;
static volatile bool s_streaming = false;
static volatile uint32_t s_stream_drop_bytes = 0;
static volatile bool s_speech_seen = false;
static volatile uint32_t s_silence_samples = 0;
static int32_t s_hpf_prev_in = 0;
static int32_t s_hpf_prev_out = 0;
static int32_t s_lpf_prev_out = 0;
static uint32_t s_noise_cal_samples = 0;
static uint64_t s_noise_accum = 0;
static uint16_t s_noise_floor = NOISE_FLOOR_MIN;
static bool s_noise_ready = false;
static bool s_gate_open = false;

static void Audio_SetTxSilence(int16_t *dst, uint32_t words);
static void Audio_FillTxFromRec(int16_t *dst, uint32_t words);
static void Audio_ProcessRxToRec(const int16_t *src, uint32_t words);
static void Audio_StreamPushMonoFromIsr(const int16_t *src, uint32_t count);
static int16_t Audio_ApplyHpfAndGate(int16_t sample);

void Audio_Init(I2S_HandleTypeDef *hi2s)
{
  s_hi2s = hi2s;
  s_i2s_started = false;
  s_recording = false;
  s_playing = false;
  s_rec_len = 0;
  s_play_idx = 0;
  s_last_peak = 0;
  Audio_StreamInit();
}

bool Audio_StartTxRxDMA(void)
{
  if (s_hi2s == NULL)
  {
    return false;
  }

  if (s_i2s_started)
  {
    return true;
  }

  Audio_SetTxSilence(s_i2s_tx, I2S_DMA_WORDS);

  if (HAL_I2SEx_TransmitReceive_DMA(s_hi2s, (uint16_t *)s_i2s_tx,
                                   (uint16_t *)s_i2s_rx, I2S_DMA_WORDS) == HAL_OK)
  {
    s_i2s_started = true;
    return true;
  }

  return false;
}

bool Audio_StopTxRxDMA(void)
{
  if ((s_hi2s == NULL) || !s_i2s_started)
  {
    return false;
  }

  (void)HAL_I2S_DMAStop(s_hi2s);
  s_i2s_started = false;
  s_recording = false;
  s_playing = false;
  s_play_idx = 0;
  return true;
}

bool Audio_IsDmaRunning(void)
{
  return s_i2s_started;
}

void Audio_RecordStart(void)
{
  s_recording = true;
  s_playing = false;
  s_rec_len = 0;
  s_play_idx = 0;
  s_speech_seen = false;
  s_silence_samples = 0;
  s_hpf_prev_in = 0;
  s_hpf_prev_out = 0;
  s_lpf_prev_out = 0;
  s_noise_cal_samples = 0;
  s_noise_accum = 0;
  s_noise_floor = NOISE_FLOOR_MIN;
  s_noise_ready = false;
  s_gate_open = false;
  Audio_StreamStart();
}

void Audio_RecordStop(void)
{
  s_recording = false;
  Audio_StreamStop();
}

bool Audio_IsRecording(void)
{
  return s_recording;
}

void Audio_PlayStart(void)
{
  s_playing = true;
  s_play_idx = 0;
}

void Audio_PlayStop(void)
{
  s_playing = false;
}

bool Audio_IsPlaying(void)
{
  return s_playing;
}

uint32_t Audio_GetRecordLength(void)
{
  return s_rec_len;
}

int16_t Audio_GetLastPeak(void)
{
  return s_last_peak;
}

const int16_t *Audio_GetRecordBuffer(void)
{
  return s_rec_buf;
}

void Audio_StreamInit(void)
{
  if (s_stream_handle != NULL)
  {
    return;
  }

  s_stream_handle = xStreamBufferCreateStatic(AUDIO_STREAM_BUFFER_BYTES,
                                              AUDIO_STREAM_TRIGGER_BYTES,
                                              s_stream_storage,
                                              &s_stream_cb);
}

void Audio_StreamStart(void)
{
  if (s_stream_handle == NULL)
  {
    Audio_StreamInit();
  }

  if (s_stream_handle != NULL)
  {
    xStreamBufferReset(s_stream_handle);
  }
  s_stream_drop_bytes = 0;
  s_streaming = true;
}

void Audio_StreamStop(void)
{
  s_streaming = false;
}

bool Audio_StreamIsActive(void)
{
  return s_streaming;
}

bool Audio_StreamIsEmpty(void)
{
  if (s_stream_handle == NULL)
  {
    return true;
  }
  return (xStreamBufferBytesAvailable(s_stream_handle) == 0U);
}

size_t Audio_StreamRead(uint8_t *dst, size_t max_len, uint32_t timeout_ms)
{
  if (dst == NULL || max_len == 0U || s_stream_handle == NULL)
  {
    return 0U;
  }
  return xStreamBufferReceive(s_stream_handle,
                              dst,
                              max_len,
                              pdMS_TO_TICKS(timeout_ms));
}

uint32_t Audio_StreamGetDroppedBytes(void)
{
  return s_stream_drop_bytes;
}

static void Audio_SetTxSilence(int16_t *dst, uint32_t words)
{
  memset(dst, 0, words * sizeof(int16_t));
}

static void Audio_FillTxFromRec(int16_t *dst, uint32_t words)
{
  for (uint32_t i = 0; i + 1 < words; i += 2)
  {
    int16_t s = 0;
    if (s_play_idx < s_rec_len)
    {
      s = s_rec_buf[s_play_idx++];
    }
    dst[i] = s;
    dst[i + 1] = s;
  }
  if (s_play_idx >= s_rec_len)
  {
    s_playing = false;
  }
}

static void Audio_ProcessRxToRec(const int16_t *src, uint32_t words)
{
  int16_t peak = 0;
  int16_t proc[I2S_DMA_MONO_SAMPLES];
  uint32_t out_idx = 0;

  for (uint32_t i = 0; i + 1 < words; i += 2)
  {
    int16_t raw = (AUDIO_RX_CHANNEL_SELECT == AUDIO_RX_CHANNEL_RIGHT) ? src[i + 1] : src[i];
    int16_t v = Audio_ApplyHpfAndGate(raw);
    if (out_idx < I2S_DMA_MONO_SAMPLES)
    {
      proc[out_idx++] = v;
    }
    int16_t a = (v < 0) ? (int16_t)(-v) : v;
    if (a > peak) peak = a;
  }
  s_last_peak = peak;

  if (out_idx > 0U && (s_streaming || s_recording))
  {
    Audio_StreamPushMonoFromIsr(proc, out_idx);
  }

  if (!s_recording)
  {
    return;
  }

  uint32_t mono_samples = out_idx;
  if (s_noise_ready)
  {
    uint32_t vad_thresh = VAD_SILENCE_PEAK;
    uint32_t dyn_thresh = (uint32_t)s_noise_floor * NOISE_GATE_OPEN_MULT;
    if (dyn_thresh > vad_thresh)
    {
      vad_thresh = dyn_thresh;
    }

    if (peak >= (int16_t)vad_thresh)
    {
      s_speech_seen = true;
      s_silence_samples = 0;
    }
    else if (s_speech_seen && s_rec_len >= VAD_MIN_SPEECH_SAMPLES)
    {
      s_silence_samples += mono_samples;
      if (s_silence_samples >= VAD_SILENCE_SAMPLES)
      {
        s_recording = false;
        s_streaming = false;
        return;
      }
    }
  }

  for (uint32_t i = 0; i < mono_samples; i++)
  {
    if (s_rec_len < REC_MAX_SAMPLES)
    {
      s_rec_buf[s_rec_len++] = proc[i];
    }
    else
    {
      s_recording = false;
      s_streaming = false;
      break;
    }
  }
}

static void Audio_StreamPushMonoFromIsr(const int16_t *src, uint32_t count)
{
  if (!s_streaming || s_stream_handle == NULL || src == NULL || count == 0U)
  {
    return;
  }

  size_t bytes = count * sizeof(int16_t);
  BaseType_t woken = pdFALSE;
  size_t written = xStreamBufferSendFromISR(s_stream_handle,
                                            src,
                                            bytes,
                                            &woken);
  if (written < bytes)
  {
    s_stream_drop_bytes += (uint32_t)(bytes - written);
  }
  portYIELD_FROM_ISR(woken);
}

static int16_t Audio_ApplyHpfAndGate(int16_t sample)
{
  int32_t x = sample;
  int32_t y = x - s_hpf_prev_in + ((HPF_A_Q15 * s_hpf_prev_out) >> 15);
  s_hpf_prev_in = x;
  s_hpf_prev_out = y;

#if LPF_ENABLE
  y = s_lpf_prev_out + ((LPF_ALPHA_Q15 * (y - s_lpf_prev_out)) >> 15);
  s_lpf_prev_out = y;
#endif

  if (y > 32767) y = 32767;
  if (y < -32768) y = -32768;

  int16_t out = (int16_t)y;
  uint16_t a = (out < 0) ? (uint16_t)(-out) : (uint16_t)out;

  if (!s_noise_ready)
  {
    s_noise_accum += a;
    s_noise_cal_samples++;
    if (s_noise_cal_samples >= NOISE_CAL_SAMPLES)
    {
      uint32_t nf = (uint32_t)(s_noise_accum / s_noise_cal_samples);
      if (nf < NOISE_FLOOR_MIN) nf = NOISE_FLOOR_MIN;
      if (nf > NOISE_FLOOR_MAX) nf = NOISE_FLOOR_MAX;
      s_noise_floor = (uint16_t)nf;
      s_noise_ready = true;
    }
    return out;
  }

  if (a < (uint32_t)s_noise_floor * NOISE_GATE_OPEN_MULT)
  {
    int32_t nf = (int32_t)s_noise_floor +
                 (((int32_t)a - (int32_t)s_noise_floor) >> NOISE_FLOOR_ALPHA_SHIFT);
    if (nf < (int32_t)NOISE_FLOOR_MIN) nf = (int32_t)NOISE_FLOOR_MIN;
    if (nf > (int32_t)NOISE_FLOOR_MAX) nf = (int32_t)NOISE_FLOOR_MAX;
    s_noise_floor = (uint16_t)nf;
  }

  uint32_t open_th = (uint32_t)s_noise_floor * NOISE_GATE_OPEN_MULT;
  uint32_t close_th = (uint32_t)s_noise_floor * NOISE_GATE_CLOSE_MULT;
  if (s_gate_open)
  {
    if (a < close_th)
    {
      s_gate_open = false;
    }
  }
  else if (a > open_th)
  {
    s_gate_open = true;
  }

  if (!s_gate_open)
  {
    out = (int16_t)((out * NOISE_ATTEN_Q15) >> 15);
    return out;
  }

  if (open_th == 0U)
  {
    return out;
  }

  if (a < open_th)
  {
    out = (int16_t)((out * (int32_t)a) / (int32_t)open_th);
  }
  return out;
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance != SPI2) return;

  Audio_ProcessRxToRec(&s_i2s_rx[0], I2S_DMA_HALF_WORDS);

  if (s_playing) Audio_FillTxFromRec(&s_i2s_tx[0], I2S_DMA_HALF_WORDS);
  else           Audio_SetTxSilence(&s_i2s_tx[0], I2S_DMA_HALF_WORDS);
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance != SPI2) return;

  Audio_ProcessRxToRec(&s_i2s_rx[I2S_DMA_HALF_WORDS], I2S_DMA_HALF_WORDS);

  if (s_playing) Audio_FillTxFromRec(&s_i2s_tx[I2S_DMA_HALF_WORDS], I2S_DMA_HALF_WORDS);
  else           Audio_SetTxSilence(&s_i2s_tx[I2S_DMA_HALF_WORDS], I2S_DMA_HALF_WORDS);
}

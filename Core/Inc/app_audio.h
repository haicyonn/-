#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define AUDIO_SAMPLE_RATE_HZ   16000U
#define AUDIO_SAMPLE_BITS      16U
#define AUDIO_SAMPLE_CHANNELS  1U

void Audio_Init(I2S_HandleTypeDef *hi2s);
bool Audio_StartTxRxDMA(void);
bool Audio_StopTxRxDMA(void);
bool Audio_IsDmaRunning(void);

void Audio_RecordStart(void);
void Audio_RecordStop(void);
bool Audio_IsRecording(void);

void Audio_PlayStart(void);
void Audio_PlayStop(void);
bool Audio_IsPlaying(void);

uint32_t Audio_GetRecordLength(void);
int16_t Audio_GetLastPeak(void);
const int16_t *Audio_GetRecordBuffer(void);

void Audio_StreamInit(void);
void Audio_StreamStart(void);
void Audio_StreamStop(void);
bool Audio_StreamIsActive(void);
bool Audio_StreamIsEmpty(void);
size_t Audio_StreamRead(uint8_t *dst, size_t max_len, uint32_t timeout_ms);
uint32_t Audio_StreamGetDroppedBytes(void);

#endif /* APP_AUDIO_H */

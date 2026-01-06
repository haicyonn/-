#include "wm8978.h"

typedef enum
{
  IN_PATH_OFF     = 0x00,
  MIC_LEFT_ON     = 0x01,
  MIC_RIGHT_ON    = 0x02,
  LINE_ON         = 0x04,
  AUX_ON          = 0x08,
  DAC_ON          = 0x10,
  ADC_ON          = 0x20
} IN_PATH_E;

typedef enum
{
  OUT_PATH_OFF    = 0x00,
  EAR_LEFT_ON     = 0x01,
  EAR_RIGHT_ON    = 0x02,
  SPK_ON          = 0x04,
  OUT3_4_ON       = 0x08
} OUT_PATH_E;

#define VOLUME_MAX   63U
#define GAIN_MAX     63U
#define MIC_GAIN_DEFAULT  146U  
#define WM8978_MIC_BOOST_ENABLE 0
#define WM8978_ADC_HPF_ENABLE 1
#define WM8978_ADC_HPF_COEFF 4U
#define WM8978_ALC_ENABLE 0
#define WM8978_NOISE_GATE_ENABLE 0
#define WM8978_ALC_TARGET 0x12U
#define WM8978_ALC_MAX_GAIN 5U
#define WM8978_ALC_MIN_GAIN 0U
#define WM8978_ALC_ATTACK 2U
#define WM8978_ALC_DECAY 3U
#define WM8978_ALC_HOLD 0U
#define WM8978_NOISE_GATE_THRESH 3U
/* Match the official recorder: enable both L/R mic inputs (diff pairs). */
#define WM8978_MIC_INPUT_MASK (MIC_LEFT_ON | MIC_RIGHT_ON)
static I2C_HandleTypeDef *s_hi2c = NULL;
static uint16_t s_addr = (0x1AU << 1);
static uint16_t s_pm2 = 0;
static bool s_out1_muted = false;

#define WM8978_OUT1_MASK ((uint16_t)((1U << 8) | (1U << 7)))

static bool WM8978_WriteReg(uint8_t reg, uint16_t val);
static void wm8978_WriteReg2(uint8_t reg, uint16_t val);
static void wm8978_PowerDown(void);
static void wm8978_CfgAudioPath(uint16_t in_path, uint16_t out_path);
static void wm8978_SetMicGain(uint8_t gain);
static void wm8978_SetOUT1Volume(uint8_t volume);
static void wm8978_ConfigAdcNoiseControl(void);

uint16_t WM8978_GetAddress(void)
{
  return s_addr;
}

void WM8978_SetOut1Mute(bool mute)
{
  if (s_hi2c == NULL)
  {
    return;
  }

  if (s_out1_muted == mute)
  {
    return;
  }

  uint16_t reg = s_pm2;
  if (mute)
  {
    reg &= (uint16_t)~WM8978_OUT1_MASK;
  }
  else
  {
    reg |= WM8978_OUT1_MASK;
  }

  wm8978_WriteReg2(2, reg);
  s_out1_muted = mute;
}

bool WM8978_Probe(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == NULL)
  {
    return false;
  }

  s_hi2c = hi2c;

  const uint16_t addrs[] = { (0x1AU << 1), (0x1BU << 1) };
  for (uint32_t i = 0; i < (sizeof(addrs) / sizeof(addrs[0])); i++)
  {
    if (HAL_I2C_IsDeviceReady(s_hi2c, addrs[i], 3, 100) == HAL_OK)
    {
      s_addr = addrs[i];
      return true;
    }
  }

  return false;
}

bool WM8978_Init(void)
{
  if (s_hi2c == NULL)
  {
    return false;
  }

  bool ok = true;

  WM8978_SetInputSource(WM8978_INPUT_MIC);

  ok &= WM8978_WriteReg(0x04, 0x010);   /* I2S format, 16-bit */
  ok &= WM8978_WriteReg(0x06, 0x000);   /* Slave mode */
  ok &= WM8978_WriteReg(0x07, 0x002);   /* Additional control */

  wm8978_SetMicGain(MIC_GAIN_DEFAULT);
  wm8978_SetOUT1Volume(55);
#if (WM8978_ADC_HPF_ENABLE || WM8978_ALC_ENABLE || WM8978_NOISE_GATE_ENABLE)
  wm8978_ConfigAdcNoiseControl();
#endif

  ok &= WM8978_WriteReg(0x0A, 0x000);

  return ok;
}

void WM8978_SetInputSource(WM8978_InputSource source)
{
  uint16_t in_path = (uint16_t)(ADC_ON | DAC_ON);

  if (source == WM8978_INPUT_LINEIN)
  {
    in_path |= LINE_ON;
  }
  else
  {
    in_path |= WM8978_MIC_INPUT_MASK;
  }

  wm8978_CfgAudioPath(in_path, (uint16_t)(EAR_LEFT_ON | EAR_RIGHT_ON));

  if (source == WM8978_INPUT_MIC)
  {
    wm8978_SetMicGain(MIC_GAIN_DEFAULT);
  }
}

static bool WM8978_WriteReg(uint8_t reg, uint16_t val)
{
  if (s_hi2c == NULL)
  {
    return false;
  }

  uint8_t buf[2];
  buf[0] = (uint8_t)((reg << 1) | ((val >> 8) & 0x01));
  buf[1] = (uint8_t)(val & 0xFF);

  return (HAL_I2C_Master_Transmit(s_hi2c, s_addr, buf, 2, 100) == HAL_OK);
}

static void wm8978_WriteReg2(uint8_t reg, uint16_t val)
{
  (void)WM8978_WriteReg(reg, val);
}

static void wm8978_PowerDown(void)
{
  (void)WM8978_WriteReg(0x00, 0x000);
  HAL_Delay(2);
}

static void wm8978_CfgAudioPath(uint16_t in_path, uint16_t out_path)
{
  uint16_t usReg;

  if ((in_path == IN_PATH_OFF) && (out_path == OUT_PATH_OFF))
  {
    wm8978_PowerDown();
    return;
  }

  usReg = (1 << 3) | (3 << 0);
  if (out_path & OUT3_4_ON)
  {
    usReg |= ((1 << 7) | (1 << 6));
  }
  if ((in_path & MIC_LEFT_ON) || (in_path & MIC_RIGHT_ON))
  {
    usReg |= (1 << 4);
  }
  wm8978_WriteReg2(1, usReg);

  usReg = 0;
  if (out_path & EAR_LEFT_ON)
  {
    usReg |= (1 << 7);
  }
  if (out_path & EAR_RIGHT_ON)
  {
    usReg |= (1 << 8);
  }
  if (in_path & MIC_LEFT_ON)
  {
    usReg |= ((1 << 4) | (1 << 2));
  }
  if (in_path & MIC_RIGHT_ON)
  {
    usReg |= ((1 << 5) | (1 << 3));
  }
  if (in_path & LINE_ON)
  {
    usReg |= ((1 << 4) | (1 << 5));
  }
  if (in_path & MIC_RIGHT_ON)
  {
    usReg |= ((1 << 5) | (1 << 3));
  }
  if (in_path & ADC_ON)
  {
    usReg |= ((1 << 1) | (1 << 0));
  }
  s_pm2 = usReg;
  wm8978_WriteReg2(2, usReg);

  usReg = 0;
  if (out_path & OUT3_4_ON)
  {
    usReg |= ((1 << 8) | (1 << 7));
  }
  if (out_path & SPK_ON)
  {
    usReg |= ((1 << 6) | (1 << 5));
  }
  if (out_path != OUT_PATH_OFF)
  {
    usReg |= ((1 << 3) | (1 << 2));
  }
  if (in_path & DAC_ON)
  {
    usReg |= ((1 << 1) | (1 << 0));
  }
  wm8978_WriteReg2(3, usReg);

  usReg = 0 << 8;
  if (in_path & LINE_ON)
  {
    usReg |= ((1 << 6) | (1 << 2));
  }
  if (in_path & MIC_RIGHT_ON)
  {
    usReg |= ((1 << 5) | (1 << 4));
  }
  if (in_path & MIC_LEFT_ON)
  {
    usReg |= ((1 << 1) | (1 << 0));
  }
  wm8978_WriteReg2(44, usReg);

  if (in_path & ADC_ON)
  {
    usReg = (1 << 3) | (WM8978_ADC_HPF_COEFF << 0);
    if (WM8978_ADC_HPF_ENABLE)
    {
      usReg |= (1 << 8);
    }
  }
  else
  {
    usReg = 0;
  }
  wm8978_WriteReg2(14, usReg);

  if (in_path & ADC_ON)
  {
    usReg = (0 << 7);
    wm8978_WriteReg2(27, usReg);
    usReg = 0;
    wm8978_WriteReg2(28, usReg);
    wm8978_WriteReg2(29, usReg);
    wm8978_WriteReg2(30, usReg);
  }

  {
    usReg = 0;
    wm8978_WriteReg2(32, usReg);
    wm8978_WriteReg2(33, usReg);
    wm8978_WriteReg2(34, usReg);
  }

  usReg = (3 << 1) | (7 << 0);
  wm8978_WriteReg2(35, usReg);

  usReg = 0;
  if (WM8978_MIC_BOOST_ENABLE &&
      ((in_path & MIC_LEFT_ON) || (in_path & MIC_RIGHT_ON)))
  {
    usReg |= (1 << 8);
  }
  if (in_path & AUX_ON)
  {
    usReg |= (3 << 0);
  }
  if (in_path & LINE_ON)
  {
    usReg |= (3 << 4);
  }
  wm8978_WriteReg2(47, usReg);
  wm8978_WriteReg2(48, usReg);

  usReg = 0xFF;
  wm8978_WriteReg2(15, usReg);
  usReg = 0x1FF;
  wm8978_WriteReg2(16, usReg);

  usReg = 0;
  if (out_path & SPK_ON)
  {
    usReg |= (1 << 4);
  }
  if (in_path & AUX_ON)
  {
    usReg |= ((7 << 1) | (1 << 0));
  }
  wm8978_WriteReg2(43, usReg);

  usReg = 0;
  if (in_path & DAC_ON)
  {
    usReg |= ((1 << 6) | (1 << 5));
  }
  if (out_path & SPK_ON)
  {
    usReg |= ((1 << 2) | (1 << 1));
  }
  if (out_path & OUT3_4_ON)
  {
    usReg |= ((1 << 4) | (1 << 3));
  }
  wm8978_WriteReg2(49, usReg);

  usReg = 0;
  if (in_path & AUX_ON)
  {
    usReg |= ((7 << 6) | (1 << 5));
  }
  if (in_path & DAC_ON)
  {
    usReg |= (1 << 0);
  }
  wm8978_WriteReg2(50, usReg);
  wm8978_WriteReg2(51, usReg);

  usReg = 0;
  if (out_path & OUT3_4_ON)
  {
    usReg |= (1 << 3);
  }
  wm8978_WriteReg2(56, usReg);

  usReg = 0;
  if (out_path & OUT3_4_ON)
  {
    usReg |= ((1 << 4) | (1 << 1));
  }
  wm8978_WriteReg2(57, usReg);

  if (in_path & DAC_ON)
  {
    wm8978_WriteReg2(11, 255);
    wm8978_WriteReg2(12, 255 | 0x100);
  }
  else
  {
    wm8978_WriteReg2(11, 0);
    wm8978_WriteReg2(12, 0 | 0x100);
  }

  if (in_path & DAC_ON)
  {
    wm8978_WriteReg2(10, 0);
  }
}

static void wm8978_SetMicGain(uint8_t gain)
{
  if (gain > GAIN_MAX)
  {
    gain = GAIN_MAX;
  }

  wm8978_WriteReg2(45, gain);
  wm8978_WriteReg2(46, gain | (1 << 8));
}

static void wm8978_SetOUT1Volume(uint8_t volume)
{
  uint16_t regL;
  uint16_t regR;

  if (volume > VOLUME_MAX)
  {
    volume = VOLUME_MAX;
  }

  regL = volume;
  regR = volume;

  wm8978_WriteReg2(52, regL | 0x00);
  wm8978_WriteReg2(53, regR | 0x100);
}

static void wm8978_ConfigAdcNoiseControl(void)
{
  uint16_t reg;

  reg = (1U << 3) | (WM8978_ADC_HPF_COEFF << 0);
  if (WM8978_ADC_HPF_ENABLE)
  {
    reg |= (1U << 8);
  }
  wm8978_WriteReg2(14, reg);

  if (WM8978_ALC_ENABLE)
  {
    reg = (1U << 8) |
          ((WM8978_ALC_MAX_GAIN & 0x7U) << 5) |
          (WM8978_ALC_TARGET & 0x1FU);
  }
  else
  {
    reg = 0;
  }
  wm8978_WriteReg2(32, reg);

  if (WM8978_ALC_ENABLE)
  {
    reg = ((WM8978_ALC_HOLD & 0xFU) << 4) |
          (WM8978_ALC_DECAY & 0xFU);
  }
  else
  {
    reg = 0;
  }
  wm8978_WriteReg2(33, reg);

  if (WM8978_ALC_ENABLE)
  {
    reg = ((WM8978_ALC_ATTACK & 0xFU) << 4) |
          (WM8978_ALC_MIN_GAIN & 0xFU);
  }
  else
  {
    reg = 0;
  }
  wm8978_WriteReg2(34, reg);

  if (WM8978_NOISE_GATE_ENABLE)
  {
    reg = (1U << 3) | (WM8978_NOISE_GATE_THRESH & 0x7U);
  }
  else
  {
    reg = 0;
  }
  wm8978_WriteReg2(35, reg);
}

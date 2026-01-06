#include "mpu6050.h"

#define MPU6050_ADDR         (0x68U << 1)
#define MPU6050_WHO_AM_I     0x75U
#define MPU6050_PWR_MGMT_1   0x6BU
#define MPU6050_ACCEL_XOUT_H 0x3BU
#define MPU6050_ACCEL_CONFIG 0x1CU
#define MPU6050_GYRO_CONFIG  0x1BU

static I2C_HandleTypeDef *s_hi2c = NULL;

static bool MPU6050_WriteReg(uint8_t reg, uint8_t val)
{
  if (s_hi2c == NULL)
  {
    return false;
  }
  return (HAL_I2C_Mem_Write(s_hi2c, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                            &val, 1, 100) == HAL_OK);
}

static bool MPU6050_ReadReg(uint8_t reg, uint8_t *buf, uint16_t len)
{
  if (s_hi2c == NULL)
  {
    return false;
  }
  return (HAL_I2C_Mem_Read(s_hi2c, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                           buf, len, 100) == HAL_OK);
}

bool MPU6050_Init(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == NULL)
  {
    return false;
  }
  s_hi2c = hi2c;

  uint8_t who = 0;
  if (!MPU6050_ReadReg(MPU6050_WHO_AM_I, &who, 1))
  {
    return false;
  }
  if (who != 0x68 && who != 0x69)
  {
    return false;
  }

  if (!MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x00))
  {
    return false;
  }
  HAL_Delay(50);

  MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00); // +/-2g
  MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x00);  // 250 dps
  return true;
}

bool MPU6050_ReadAccelRaw(int16_t *ax, int16_t *ay, int16_t *az)
{
  if (s_hi2c == NULL)
  {
    return false;
  }

  uint8_t buf[6];
  if (!MPU6050_ReadReg(MPU6050_ACCEL_XOUT_H, buf, 6))
  {
    return false;
  }

  *ax = (int16_t)((buf[0] << 8) | buf[1]);
  *ay = (int16_t)((buf[2] << 8) | buf[3]);
  *az = (int16_t)((buf[4] << 8) | buf[5]);
  return true;
}

void MPU6050_Calibrate(int16_t *ax_off, int16_t *ay_off)
{
  if (ax_off == NULL || ay_off == NULL)
  {
    return;
  }

  *ax_off = 0;
  *ay_off = 0;

  if (s_hi2c == NULL)
  {
    return;
  }

  int32_t sum_x = 0;
  int32_t sum_y = 0;
  const uint16_t samples = 100;

  for (uint16_t i = 0; i < samples; i++)
  {
    int16_t ax = 0;
    int16_t ay = 0;
    int16_t az = 0;
    if (MPU6050_ReadAccelRaw(&ax, &ay, &az))
    {
      sum_x += ax;
      sum_y += ay;
    }
    HAL_Delay(5);
  }

  *ax_off = (int16_t)(sum_x / (int32_t)samples);
  *ay_off = (int16_t)(sum_y / (int32_t)samples);
}

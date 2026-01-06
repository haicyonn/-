#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

bool MPU6050_Init(I2C_HandleTypeDef *hi2c);
bool MPU6050_ReadAccelRaw(int16_t *ax, int16_t *ay, int16_t *az);
void MPU6050_Calibrate(int16_t *ax_off, int16_t *ay_off);

#endif /* MPU6050_H */

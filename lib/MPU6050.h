#ifndef MPU6050_H
#define MPU6050_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define MPU6050_ADDR 0x68

// Registros do MPU6050
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_GYRO_XOUT_H  0x43

// Escalas do acelerômetro
enum mpu6050_accel_scale {
    AFS_2G = 0,
    AFS_4G,
    AFS_8G,
    AFS_16G
};

// Escalas do giroscópio
enum mpu6050_gyro_scale {
    GFS_250DPS = 0,
    GFS_500DPS,
    GFS_1000DPS,
    GFS_2000DPS
};

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    enum mpu6050_accel_scale accel_scale;
    enum mpu6050_gyro_scale gyro_scale;
    float accel_sensitivity;
    float gyro_sensitivity;
} mpu6050_t;

// Protótipos das funções
void mpu6050_init(mpu6050_t *mpu, i2c_inst_t *i2c, uint8_t addr, 
                 enum mpu6050_accel_scale accel_scale, 
                 enum mpu6050_gyro_scale gyro_scale);
void mpu6050_reset(mpu6050_t *mpu);
void mpu6050_wake_up(mpu6050_t *mpu);
void mpu6050_read_raw(mpu6050_t *mpu, int16_t *accel, int16_t *gyro);
void mpu6050_read_calibrated(mpu6050_t *mpu, float *accel, float *gyro);
void mpu6050_calibrate(mpu6050_t *mpu, int16_t *accel_bias, int16_t *gyro_bias, uint16_t samples);

#endif // MPU6050_H
#include "MPU6050.h"
#include <math.h>

// Fatores de escala para conversão
static const float ACCEL_SENSITIVITY[] = {
    [AFS_2G] = 16384.0f,
    [AFS_4G] = 8192.0f,
    [AFS_8G] = 4096.0f,
    [AFS_16G] = 2048.0f
};

static const float GYRO_SENSITIVITY[] = {
    [GFS_250DPS] = 131.0f,
    [GFS_500DPS] = 65.5f,
    [GFS_1000DPS] = 32.8f,
    [GFS_2000DPS] = 16.4f
};

// Função auxiliar para escrita em registros
static void mpu6050_write_register(mpu6050_t *mpu, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    i2c_write_blocking(mpu->i2c, mpu->addr, buf, 2, false);
}

// Função auxiliar para leitura de registros
static void mpu6050_read_registers(mpu6050_t *mpu, uint8_t reg, uint8_t *buf, uint8_t len) {
    i2c_write_blocking(mpu->i2c, mpu->addr, &reg, 1, true);
    i2c_read_blocking(mpu->i2c, mpu->addr, buf, len, false);
}

void mpu6050_init(mpu6050_t *mpu, i2c_inst_t *i2c, uint8_t addr, 
                 enum mpu6050_accel_scale accel_scale, 
                 enum mpu6050_gyro_scale gyro_scale) {
    mpu->i2c = i2c;
    mpu->addr = addr;
    mpu->accel_scale = accel_scale;
    mpu->gyro_scale = gyro_scale;
    mpu->accel_sensitivity = ACCEL_SENSITIVITY[accel_scale];
    mpu->gyro_sensitivity = GYRO_SENSITIVITY[gyro_scale];
    
    // Acorda o MPU6050
    mpu6050_wake_up(mpu);
    
    // Configura escalas
    mpu6050_write_register(mpu, MPU6050_REG_ACCEL_CONFIG, mpu->accel_scale << 3);
    mpu6050_write_register(mpu, MPU6050_REG_GYRO_CONFIG, mpu->gyro_scale << 3);
}

void mpu6050_reset(mpu6050_t *mpu) {
    mpu6050_write_register(mpu, MPU6050_REG_PWR_MGMT_1, 0x80);
    sleep_ms(100);
}

void mpu6050_wake_up(mpu6050_t *mpu) {
    mpu6050_write_register(mpu, MPU6050_REG_PWR_MGMT_1, 0x00);
    sleep_ms(100);
}

void mpu6050_read_raw(mpu6050_t *mpu, int16_t *accel, int16_t *gyro) {
    uint8_t buffer[14];
    
    // Lê todos os registros de dados de uma vez
    mpu6050_read_registers(mpu, MPU6050_REG_ACCEL_XOUT_H, buffer, 14);
    
    // Converte os dados para inteiros de 16 bits
    accel[0] = (int16_t)((buffer[0] << 8) | buffer[1]);  // Accel X
    accel[1] = (int16_t)((buffer[2] << 8) | buffer[3]);  // Accel Y
    accel[2] = (int16_t)((buffer[4] << 8) | buffer[5]);  // Accel Z
    
    gyro[0] = (int16_t)((buffer[8] << 8) | buffer[9]);    // Gyro X
    gyro[1] = (int16_t)((buffer[10] << 8) | buffer[11]);  // Gyro Y
    gyro[2] = (int16_t)((buffer[12] << 8) | buffer[13]);  // Gyro Z
}

void mpu6050_read_calibrated(mpu6050_t *mpu, float *accel, float *gyro) {
    int16_t raw_accel[3], raw_gyro[3];
    
    mpu6050_read_raw(mpu, raw_accel, raw_gyro);
    
    // Converte para valores reais (g e dps)
    for (int i = 0; i < 3; i++) {
        accel[i] = (float)raw_accel[i] / mpu->accel_sensitivity;
        gyro[i] = (float)raw_gyro[i] / mpu->gyro_sensitivity;
    }
}

void mpu6050_calibrate(mpu6050_t *mpu, int16_t *accel_bias, int16_t *gyro_bias, uint16_t samples) {
    int32_t accel_sum[3] = {0};
    int32_t gyro_sum[3] = {0};
    
    for (uint16_t i = 0; i < samples; i++) {
        int16_t raw_accel[3], raw_gyro[3];
        mpu6050_read_raw(mpu, raw_accel, raw_gyro);
        
        for (int j = 0; j < 3; j++) {
            accel_sum[j] += raw_accel[j];
            gyro_sum[j] += raw_gyro[j];
        }
        
        sleep_ms(10);
    }
    
    for (int i = 0; i < 3; i++) {
        accel_bias[i] = (int16_t)(accel_sum[i] / samples);
        gyro_bias[i] = (int16_t)(gyro_sum[i] / samples);
    }
    
    // O eixo Z do acelerômetro deve ter aproximadamente 1g (ajuste para escala 2G)
    accel_bias[2] -= (int16_t)(1.0f * mpu->accel_sensitivity);
}
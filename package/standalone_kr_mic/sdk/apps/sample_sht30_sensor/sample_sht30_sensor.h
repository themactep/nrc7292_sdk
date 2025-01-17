#ifndef _SAMPLE_SHT30_SENSOR_H
#define _SAMPLE_SHT30_SENSOR_H

#define SENSOR_I2C_SCL 16
#define SENSOR_I2C_SDA 17
#define SENSOR_I2C_CLOCK 100000
#define SENSOR_I2C_CLOCK_SOURCE 0 /* 0:clock controller, 1:PCLK */
#define I2C_XACT_DELAY_MS 1

#define SENSOR_OK 0
#define SENSOR_FAIL -1

#endif /* _SAMPLE_SHT30_SENSOR_H */

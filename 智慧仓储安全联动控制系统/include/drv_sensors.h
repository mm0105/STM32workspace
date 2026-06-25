#ifndef __DRV_SENSORS_H__
#define __DRV_SENSORS_H__


void i2c_dev_init(void);
void bh1750_read_data(double *dat);
void sht30_read_data(double *temp, double *humi);

void mq2_dev_init(void);
void mq2_ppm_calibration(void);
float get_mq2_ppm(void);

#endif
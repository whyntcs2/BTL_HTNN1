#ifndef I2C_H
#define I2C_H


#include "stm32f10x.h"

typedef enum {
    I2C_OK = 0,
    I2C_ERR_TIMEOUT         /* thiet bi khong tra loi / NACK / bus ket */
} I2C_Status;


void       i2c_init(uint32_t ulClockHz);

I2C_Status i2c_start(uint8_t ucAddr7);
I2C_Status i2c_write(uint8_t ucData);
void       i2c_stop(void);
void       i2c_recover(void);

#endif 

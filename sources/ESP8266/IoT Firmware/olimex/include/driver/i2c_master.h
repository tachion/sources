#ifndef __I2C_MASTER_H__
	#define __I2C_MASTER_H__

	#include "user_config.h"

	#ifdef DIMMER_REV_A  // DIMMER - Revision A
		#define I2C_MASTER_SDA_MUX PERIPHS_IO_MUX_U0RXD_U
		#define I2C_MASTER_SCL_MUX PERIPHS_IO_MUX_U0TXD_U
		#define I2C_MASTER_SDA_GPIO 3
		#define I2C_MASTER_SCL_GPIO 1
		#define I2C_MASTER_SDA_FUNC FUNC_GPIO3
		#define I2C_MASTER_SCL_FUNC FUNC_GPIO1
	#else
		#define I2C_MASTER_SDA_MUX PERIPHS_IO_MUX_GPIO2_U
		#define I2C_MASTER_SCL_MUX PERIPHS_IO_MUX_GPIO4_U
		#define I2C_MASTER_SDA_GPIO 2
		#define I2C_MASTER_SCL_GPIO 4
		#define I2C_MASTER_SDA_FUNC FUNC_GPIO2
		#define I2C_MASTER_SCL_FUNC FUNC_GPIO4
	#endif

	#define I2C_MASTER_SDA_HIGH_SCL_HIGH()  \
		gpio_output_set(1<<I2C_MASTER_SDA_GPIO | 1<<I2C_MASTER_SCL_GPIO, 0, 1<<I2C_MASTER_SDA_GPIO | 1<<I2C_MASTER_SCL_GPIO, 0)

	#define I2C_MASTER_SDA_HIGH_SCL_LOW()  \
		gpio_output_set(1<<I2C_MASTER_SDA_GPIO, 1<<I2C_MASTER_SCL_GPIO, 1<<I2C_MASTER_SDA_GPIO | 1<<I2C_MASTER_SCL_GPIO, 0)

	#define I2C_MASTER_SDA_LOW_SCL_HIGH()  \
		gpio_output_set(1<<I2C_MASTER_SCL_GPIO, 1<<I2C_MASTER_SDA_GPIO, 1<<I2C_MASTER_SDA_GPIO | 1<<I2C_MASTER_SCL_GPIO, 0)

	#define I2C_MASTER_SDA_LOW_SCL_LOW()  \
		gpio_output_set(0, 1<<I2C_MASTER_SDA_GPIO | 1<<I2C_MASTER_SCL_GPIO, 1<<I2C_MASTER_SDA_GPIO | 1<<I2C_MASTER_SCL_GPIO, 0)

	void i2c_master_gpio_init(void);
	void i2c_master_init(void);

	#define i2c_master_wait    os_delay_us
	void i2c_master_stop(void);
	void i2c_master_start(void);
	void i2c_master_setAck(uint8 level);
	uint8 i2c_master_getAck(void);
	uint8 i2c_master_readByte(void);
	void i2c_master_writeByte(uint8 wrdata);

	bool i2c_master_checkAck(void);
	void i2c_master_send_ack(void);
	void i2c_master_send_nack(void);

#endif

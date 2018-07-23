/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bip board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "driver/bc12/bq24392.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/usb_mux_it5205.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tcpci.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "uart.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_PD_INT_ODL)
		sn5s330_interrupt(0);
	else if (signal == GPIO_USB_C1_PD_INT_ODL)
		sn5s330_interrupt(1);
}

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus C0 sensing (10x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = {
		"VBUS_C0", 10*ADC_MAX_MVOLT, ADC_READ_MAX+1, 0, CHIP_ADC_CH13},
	/* Vbus C1 sensing (10x voltage divider). PPVAR_USB_C1_VBUS */
	[ADC_VBUS_C1] = {
		"VBUS_C1", 10*ADC_MAX_MVOLT, ADC_READ_MAX+1, 0, CHIP_ADC_CH14},
	/* Convert to mV (3300mV/1024). */
	[ADC_TEMP_SENSOR_AMB] = { "TEMP_AMB", 3300, 1024, 0, CHIP_ADC_CH3},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_AMBIENT] = {.name = "Ambient",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_51k1_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_AMB,
				 .action_delay_sec = 5},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/******************************************************************************/
/* SPI devices */
/* TODO(b/75972988): Fill out correctly (SPI FLASH) */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

void board_overcurrent_event(int port)
{
	/* TODO(b/78344554): pass this signal upstream once hardware reworked */
	cprints(CC_USBPD, "p%d: overcurrent!", port);
}

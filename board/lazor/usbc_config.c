/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lazor board-specific USB-C configuration */

#include "bc12/pi3usb9201_public.h"
#include "charge_state.h"
#include "common.h"
#include "config.h"
#include "gpio.h"
#include "ppc/sn5s330_public.h"
#include "tcpm/ps8xxx_public.h"
#include "tcpm/tcpci.h"
#include "timer.h"
#include "usb_pd.h"
#include "usbc_config.h"
#include "usb_mux.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* GPIO Interrupt Handlers */
void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void board_reset_pd_mcu(void)
{
	cprints(CC_USB, "Resetting TCPCs...");
	cflush();

	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	msleep(PS8XXX_RESET_DELAY_MS);
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
}

void board_set_tcpc_power_mode(int port, int mode)
{
	/* Ignore the "mode" to turn the chip on.  We can only do a reset. */
	if (mode)
		return;

	board_reset_pd_mcu();
}

int board_vbus_sink_enable(int port, int enable)
{
	/* Both ports are controlled by PPC SN5S330 */
	return ppc_vbus_sink_enable(port, enable);
}

int board_is_sourcing_vbus(int port)
{
	/* Both ports are controlled by PPC SN5S330 */
	return ppc_is_sourcing_vbus(port);
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO(b/120231371): Notify AP */
	CPRINTS("p%d: overcurrent!", port);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charging port");

		/* Disable all ports. */
		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (board_vbus_sink_enable(i, 0))
				CPRINTS("Disabling p%d sink path failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}


	CPRINTS("New charge port: p%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (i == port)
			continue;

		if (board_vbus_sink_enable(i, 0))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (board_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/*
	 * Ignore lower charge ceiling on PD transition if our battery is
	 * critical, as we may brownout.
	 */
	if (supplier == CHARGE_SUPPLIER_PD &&
	    charge_ma < 1500 &&
	    charge_get_percent() < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		CPRINTS("Using max ilim %d", max_ma);
		charge_ma = max_ma;
	}

	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

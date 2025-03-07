/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Myst board-specific USB-C mux configuration */

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usbc/ppc.h"
#include "usbc/tcpci.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(myst, CONFIG_MYST_LOG_LEVEL);

uint32_t get_io_db_type_from_cached_cbi(void)
{
	uint32_t io_db_type;
	int ret = cros_cbi_get_fw_config(FW_IO_DB, &io_db_type);

	if (ret != 0) {
		io_db_type = FW_IO_DB_NONE;
		LOG_ERR("Failed to get IO_DB value: %d", ret);
	}

	return io_db_type;
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (get_io_db_type_from_cached_cbi() == FW_IO_DB_NONE)
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT;
}

void ppc_interrupt(enum gpio_signal signal)
{
	uint32_t io_db_type = get_io_db_type_from_cached_cbi();

	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		ktu1125_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		if (io_db_type == FW_IO_DB_SKU_A) {
			nx20p348x_interrupt(USBC_PORT_C1);
		}
		if (io_db_type == FW_IO_DB_SKU_B) {
			ktu1125_interrupt(USBC_PORT_C1);
		}
		break;

	default:
		break;
	}
}

static void setup_mux(void)
{
	switch (get_io_db_type_from_cached_cbi()) {
	default:
	case FW_IO_DB_NONE:
		LOG_INF("USB DB: not connected");
		break;

	case FW_IO_DB_SKU_A:
		LOG_INF("USB DB: Setting SKU_A DB");
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_rt1718_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						  ppc_nx20p_port1);
		break;

	case FW_IO_DB_SKU_B:
		LOG_INF("USB DB: Setting SKU_B DB");
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_ps8815_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						  ppc_ktu1125_port1);
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, setup_mux, HOOK_PRIO_INIT_I2C);

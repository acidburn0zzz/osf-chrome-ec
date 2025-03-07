/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rex board-specific USB-C mux configuration */

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usb_mux_config.h"
#include "usb_pd.h"
#include "usbc/ppc.h"
#include "usbc/tcpci.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/gpio_defines.h>

#ifdef CONFIG_ZTEST

#undef USB_MUX_ENABLE_ALTERNATIVE
#define USB_MUX_ENABLE_ALTERNATIVE(x)

#undef TCPC_ENABLE_ALTERNATE_BY_NODELABEL
#define TCPC_ENABLE_ALTERNATE_BY_NODELABEL(x, y)

#undef PPC_ENABLE_ALTERNATE_BY_NODELABEL
#define PPC_ENABLE_ALTERNATE_BY_NODELABEL(x, y)

#endif /* CONFIG_ZTEST */

LOG_MODULE_DECLARE(rex, CONFIG_REX_LOG_LEVEL);

uint32_t usb_db_type;

static int gpio_unused(const struct gpio_dt_spec *spec)
{
	return gpio_pin_configure(spec->port, spec->pin, GPIO_INPUT_PULL_UP);
}

static void setup_runtime_gpios(void)
{
	int ret;

	ret = cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type);
	if (ret != EC_SUCCESS) {
		LOG_INF("Failed to get FW_USB_DB from CBI");
		usb_db_type = FW_USB_DB_NOT_CONNECTED;
	}

	switch (usb_db_type) {
	case FW_USB_DB_USB3:
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rst_odl));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_int_odl));
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(ps_usb_c1_rt_rst_odl),
				      GPIO_ODR_HIGH | GPIO_ACTIVE_LOW);
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_frs_en));
		break;

	case FW_USB_DB_USB4_KB8010:
		gpio_unused(GPIO_DT_FROM_ALIAS(kb_usb_c1_rst_odl));
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(kb_usb_c1_dp_mode),
				      GPIO_ODR_HIGH);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(kb_usb_c1_rt_rst_odl),
				      GPIO_ODR_HIGH);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(kb_usb_c1_frs_alert),
				      GPIO_INPUT);
		break;

	case FW_USB_DB_USB4_ANX7452:
	case FW_USB_DB_USB4_ANX7452_V2:
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(anx_usb_c1_rt_hpd_in),
				      GPIO_OUTPUT_LOW);
		gpio_unused(GPIO_DT_FROM_ALIAS(anx_usb_c1_rt_dp_en));
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(anx_usb_c1_rt_usb_en),
				      GPIO_OUTPUT_LOW);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(anx_usb_c1_frs_en),
				      GPIO_OUTPUT_LOW);
		break;

	default:
		/* GPIO37 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rst_odl));
		/* GPIO72 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_int_odl));
		/* GPIO74 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_rst_r_odl));
		/* GPIO83 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_frs_en));
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, setup_runtime_gpios, HOOK_PRIO_FIRST);

static void setup_usb_db(void)
{
	switch (usb_db_type) {
	case FW_USB_DB_NOT_CONNECTED:
		LOG_INF("USB DB: not connected");
		break;
	case FW_USB_DB_USB3:
		LOG_INF("USB DB: Setting USB3 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_ps8815_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_ps8815_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						  ppc_nx20p_port1);
		break;
	case FW_USB_DB_USB4_ANX7452:
	case FW_USB_DB_USB4_ANX7452_V2:
		LOG_INF("USB DB: Setting ANX7452 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_anx7452_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_rt1716_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1, ppc_syv_port1);
		break;
	case FW_USB_DB_USB4_KB8010:
		LOG_INF("USB DB: Setting KB8010 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_kb8010_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_rt1716_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						  ppc_ktu1125_port1);
		break;
	default:
		LOG_INF("USB DB: No known USB DB found");
	}
}
DECLARE_HOOK(HOOK_INIT, setup_usb_db, HOOK_PRIO_POST_I2C);

__override uint8_t board_get_usb_pd_port_count(void)
{
	switch (usb_db_type) {
	case FW_USB_DB_USB3:
	case FW_USB_DB_USB4_ANX7452:
	case FW_USB_DB_USB4_ANX7452_V2:
	case FW_USB_DB_USB4_KB8010:
		return 2;
	default:
		return 1;
	}
}

/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "anx7406.h"
#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/nvidia_gpu.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "throttle_ap.h"
#include "usbc_config.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

static int block_sequence;

struct d_notify_policy d_notify_policies[] = {
	[D_NOTIFY_1] = AC_ATLEAST_W(100),
	[D_NOTIFY_2] = AC_ATLEAST_W(65),
	[D_NOTIFY_3] = AC_DC,
	[D_NOTIFY_4] = DC_ATMOST_SOC(20),
	[D_NOTIFY_5] = DC_ATMOST_SOC(5),
};
BUILD_ASSERT(ARRAY_SIZE(d_notify_policies) == D_NOTIFY_COUNT);

__override void board_cbi_init(void)
{
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */
	gpio_set_level(GPIO_EN_EC_KB_BL_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */
	gpio_set_level(GPIO_EN_EC_KB_BL_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void board_init(void)
{
	const uint16_t i2c_sequencer_addr_flag = 0x10;
	/* 0xaf: glitch filter reg address */
	const uint8_t out[2] = { 0xaf, 0x01 };

	int rv;

	if ((system_get_reset_flags() & EC_RESET_FLAG_AP_OFF) ||
	    (keyboard_scan_get_boot_keys() & BOOT_KEY_DOWN_ARROW)) {
		CPRINTS("PG_PP3300_S5_OD block is enabled");
		block_sequence = 1;
	}

	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_ODL);

	nvidia_gpu_init_policy(d_notify_policies);

	/* Unblock USB_C1_PPC_SNK_EN */
	anx7406_set_gpio(USBC_PORT_C1, 0, 1);

	/* Adjust glitch filtering on PPVAR_SYS (b/282181312) */
	rv = i2c_xfer(I2C_PORT_MISC, i2c_sequencer_addr_flag, out, sizeof(out),
		      NULL, 0);
	if (rv)
		CPRINTS("Failed to adjust sequencer timing (%d)", rv);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void gpu_overt_interrupt(enum gpio_signal signal)
{
	nvidia_gpu_over_temp(gpio_get_level(signal));
}

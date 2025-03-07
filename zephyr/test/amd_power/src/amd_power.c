/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_app_main.h"
#include "emul/emul_stub_device.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "host_command.h"
#include "include/power_button.h"
#include "lid_switch.h"
#include "power.h"
#include "power/amd_x86.h"
#include "task.h"

#include <setjmp.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include <dt-bindings/buttons.h>

/* All emulated GPIOS are on one device */
#define GPIO_DEVICE \
	DEVICE_DT_GET(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(s0_pgood), gpios))
#define SLP_S3_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(slp_s3_l), gpios)
#define SLP_S5_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(slp_s5_l), gpios)
#define PGOOD_S0_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(s0_pgood), gpios)
#define PGOOD_S5_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(pg_pwr_s5), gpios)
#define PWRBTN_IN_PIN \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(mech_pwr_btn_odl), gpios)
#define PWRBTN_OUT_PIN \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ec_soc_pwr_btn_l), gpios)
#define PROCHOT_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(prochot_odl), gpios)
#define LID_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(lid_open_ec), gpios)

/*
 * Provide standard array of power signals for the module based on our DTS enum
 * names we filled in
 */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

struct hook_tracker {
	int startup_count;
	int resume_count;
	int reset_count;
	int suspend_count;
	int shutdown_count;
};

static struct hook_tracker hook_counts;

static void do_chipset_startup(void)
{
	hook_counts.startup_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, do_chipset_startup, HOOK_PRIO_DEFAULT);

static void do_chipset_resume(void)
{
	hook_counts.resume_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, do_chipset_resume, HOOK_PRIO_DEFAULT);

static void do_chipset_reset(void)
{
	hook_counts.reset_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, do_chipset_reset, HOOK_PRIO_DEFAULT);

static void do_chipset_suspend(void)
{
	hook_counts.suspend_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, do_chipset_suspend, HOOK_PRIO_DEFAULT);

static void do_chipset_shutdown(void)
{
	hook_counts.shutdown_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, do_chipset_shutdown, HOOK_PRIO_DEFAULT);

FAKE_VALUE_FUNC(int, system_can_boot_ap);
FAKE_VALUE_FUNC(int, system_jumped_to_this_image);
FAKE_VALUE_FUNC(int, battery_wait_for_stable);
int battery_is_present(void)
{
	return 1;
}

void amd_power_before(void *fixture)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 1;
	RESET_FAKE(system_jumped_to_this_image);
	system_jumped_to_this_image_fake.return_val = 0;

	memset(&hook_counts, 0, sizeof(hook_counts));

	/* Start GPIOs out in G3, lid open */
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S5_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S0_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, LID_PIN, 1));
	power_set_state(POWER_G3);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
	zassert_equal(power_has_signals(POWER_SIGNAL_MASK(0)), 0);
}

void amd_power_after(void *fixture)
{
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));
	init_reset_log();
	system_clear_reset_flags(EC_RESET_FLAG_AP_OFF);
}

ZTEST_SUITE(amd_power, NULL, NULL, amd_power_before, amd_power_after, NULL);

ZTEST(amd_power, test_power_chipset_init_ap_off)
{
	system_set_reset_flags(EC_RESET_FLAG_AP_OFF);
	zassert_equal(power_chipset_init(), POWER_G3);
	power_set_state(POWER_G3);

	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

/* General helper to get us up to S0 */
static void amd_power_s0_on(void)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* "press" the power button */
	zassert_ok(gpio_emul_input_set(gpio_dev, PWRBTN_IN_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 0);

	/* and "release" */
	zassert_ok(gpio_emul_input_set(gpio_dev, PWRBTN_IN_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 1);

	/* Observe we're heading up and toggle appropriate "soc" outputs */
	zassert_equal(power_get_state(), POWER_G3S5, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 1));
	k_sleep(K_MSEC(500));

	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S5_PIN, 1));
	k_sleep(K_MSEC(500));

	/* Verify hook_notify calls that come from the AMD power file */
	zassert_equal(hook_counts.startup_count, 1);
	zassert_equal(power_get_state(), POWER_S3, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 1));
	k_sleep(K_MSEC(500));

	zassert_equal(hook_counts.resume_count, 1);
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S0_PIN, 1));
}

ZTEST(amd_power, test_power_happy_s0_path)
{
	amd_power_s0_on();
}

ZTEST(amd_power, test_power_s5_power_loss_in_s5)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* "press" the power button */
	zassert_ok(gpio_emul_input_set(gpio_dev, PWRBTN_IN_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 0);

	/* and "release" */
	zassert_ok(gpio_emul_input_set(gpio_dev, PWRBTN_IN_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 1);

	/* Observe we're heading up and toggle appropriate "soc" outputs */
	zassert_equal(power_get_state(), POWER_G3S5, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 1));
	k_sleep(K_MSEC(500));

	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());

	/* But now we've lost S5 power good, so go to G3 */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

ZTEST(amd_power, test_power_s5_power_loss_in_s3)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* "press" the power button */
	zassert_ok(gpio_emul_input_set(gpio_dev, PWRBTN_IN_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 0);

	/* and "release" */
	zassert_ok(gpio_emul_input_set(gpio_dev, PWRBTN_IN_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 1);

	/* Observe we're heading up and toggle appropriate "soc" outputs */
	zassert_equal(power_get_state(), POWER_G3S5, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 1));
	k_sleep(K_MSEC(500));

	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S5_PIN, 1));
	k_sleep(K_MSEC(500));

	/* Verify hook_notify calls that come from the AMD power file */
	zassert_equal(hook_counts.startup_count, 1);
	zassert_equal(power_get_state(), POWER_S3, "power_state=%d",
		      power_get_state());

	/* But now we've lost S5 power good, so go to G3 */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

ZTEST(amd_power, test_power_s5_loss_in_s0)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* But now we've lost S5 power good, so go to G3 */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

ZTEST(amd_power, test_power_happy_shutdown)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* Start de-sequencing with S0 PGOOD and SLP_S3 */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S0_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 0));
	k_sleep(K_MSEC(500));

	/* Verify hook_notify calls that come from the AMD power file */
	zassert_equal(hook_counts.suspend_count, 1);
	zassert_equal(power_get_state(), POWER_S3, "power_state=%d",
		      power_get_state());

	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S5_PIN, 0));
	k_sleep(K_MSEC(500));

	zassert_equal(hook_counts.shutdown_count, 1);
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());

	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

ZTEST(amd_power, test_power_happy_suspend_resume)
{
	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* Sleepy time */
	zassert_ok(host_command_process(&host_sleep_ev_args));
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 0));
	k_sleep(K_MSEC(500));

	zassert_equal(hook_counts.suspend_count, 1);
	zassert_equal(power_get_state(), POWER_S0ix, "power_state=%d",
		      power_get_state());

	/* And time to wake */
	hook_counts.resume_count = 0;
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 1));
	k_sleep(K_MSEC(500));
	host_sleep_ev_p.sleep_event = HOST_SLEEP_EVENT_S0IX_RESUME;
	zassert_ok(host_command_process(&host_sleep_ev_args));
	k_sleep(K_MSEC(500));

	zassert_equal(hook_counts.resume_count, 1);
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
}

ZTEST(amd_power, test_power_suspend_power_loss)
{
	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* Sleepy time */
	zassert_ok(host_command_process(&host_sleep_ev_args));
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 0));
	k_sleep(K_MSEC(500));

	zassert_equal(hook_counts.suspend_count, 1);
	zassert_equal(power_get_state(), POWER_S0ix, "power_state=%d",
		      power_get_state());

	/* Oh no!  S5 power has been lost cap'n */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

ZTEST(amd_power, test_power_suspend_hang)
{
	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);
	const char *buffer;
	size_t buffer_size;

	amd_power_s0_on();

	/* Send sleep event, but fail to actually transition the signal */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(host_command_process(&host_sleep_ev_args));
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

	zassert_equal(power_get_state(), POWER_S0);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "Detected sleep hang!") != NULL);
}

ZTEST(amd_power, test_power_forced_shutdown)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* Report a critical thermal event */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
	k_sleep(K_MSEC(500));

	/* Chipset task sends the power button to the processor */
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 0);
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_SHUTDOWN_THERMAL);

	/* Allow our rails to turn off for shutdown */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S0_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S3_PIN, 0));
	k_sleep(K_MSEC(500));

	zassert_ok(gpio_emul_input_set(gpio_dev, SLP_S5_PIN, 0));
	k_sleep(K_MSEC(500));

	/* Power button should be released now that we shut down */
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 1);
}

ZTEST(amd_power, test_power_forced_shutdown_espi_reset)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* Report a critical thermal event */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
	k_sleep(K_MSEC(500));

	/* Chipset task sends the power button to the processor */
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 0);
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_SHUTDOWN_THERMAL);

	/*
	 * Before our rails went down, we got an eSPI reset assert which removes
	 * the power button assert since the processor is shutting down
	 */
	chipset_handle_espi_reset_assert();
	zassert_equal(gpio_emul_output_get(gpio_dev, PWRBTN_OUT_PIN), 1);
}

ZTEST(amd_power, test_power_chipset_reset_s0)
{
	amd_power_s0_on();

	/* Report a special keyboard reset */
	chipset_reset(CHIPSET_RESET_KB_SYSRESET);
	k_sleep(K_MSEC(500));

	/* Verify our reporting */
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_RESET_KB_SYSRESET);
}

ZTEST(amd_power, test_power_chipset_reset_g3)
{
	/* Report a special keyboard reset */
	chipset_reset(CHIPSET_RESET_KB_SYSRESET);
	k_sleep(K_MSEC(500));

	/* Verify we didn't report the reset attempt */
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_RESET_UNKNOWN);
}

ZTEST(amd_power, test_power_chipset_throttle_s0)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	amd_power_s0_on();

	/* Report we need to throttle */
	chipset_throttle_cpu(1);

	/* Verify we see PROCHOT asserted */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S0_PIN, 0));
}

ZTEST(amd_power, test_power_chipset_throttle_g3)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* Report we need to throttle */
	chipset_throttle_cpu(1);

	/* Verify we ignored it since we're off */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S0_PIN, 1));
}

ZTEST(amd_power, test_sysjump_s0)
{
	/* Simulate a "sysjump" in S0 */
	amd_power_s0_on();

	RESET_FAKE(system_jumped_to_this_image);
	system_jumped_to_this_image_fake.return_val = 1;
	zassert_equal(power_chipset_init(), POWER_S0);
}

ZTEST(amd_power, test_sysjump_s5)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* Only set S5 PGOOD this time */
	zassert_ok(gpio_emul_input_set(gpio_dev, PGOOD_S5_PIN, 1));

	RESET_FAKE(system_jumped_to_this_image);
	system_jumped_to_this_image_fake.return_val = 1;
	zassert_equal(power_chipset_init(), POWER_S5);
}

ZTEST(amd_power, test_sysjump_g3)
{
	/* "Sysjump" with no power rails on */
	RESET_FAKE(system_jumped_to_this_image);
	system_jumped_to_this_image_fake.return_val = 1;
	zassert_equal(power_chipset_init(), POWER_G3);
}

ZTEST(amd_power, test_lid_open_power_on)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* "close" our lid and observe we're still in G3 */
	zassert_ok(gpio_emul_input_set(gpio_dev, LID_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());

	/* "open" and observe we try to power on */
	zassert_ok(gpio_emul_input_set(gpio_dev, LID_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3S5, "power_state=%d",
		      power_get_state());
}

void test_main(void)
{
	ec_app_main();
	/*
	 * Fake sleep long enough to ensure all automatic power sequencing is
	 * done
	 */
	k_sleep(K_SECONDS(11));

	ztest_run_test_suites(NULL);

	ztest_verify_all_test_suites_ran();
}

/* These 2 lines are needed because we don't define an espi host driver */
#define DT_DRV_COMPAT zephyr_espi_emul_espi_host
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "keyboard_test_utils.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include <emul/emul_kb_raw.h>

ZTEST(keyboard_scan, test_boot_key)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(cros_kb_raw));
	const int kb_cols = DT_PROP(DT_NODELABEL(cros_kb_raw), cols);

	emul_kb_raw_reset(dev);
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE, NULL);

	/* Case 1: refresh + esc -> BOOT_KEY_ESC */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true));
	zassert_ok(emulate_keystate(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, true));
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_ESC);

	/*
	 * Case 1.5:
	 * GSC may hold ksi2 when power button is pressed, simulate this
	 * behavior and verify boot key detection again.
	 */
	zassert_true(IS_ENABLED(CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2), NULL);
	for (int i = 0; i < kb_cols; i++) {
		zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, i, true));
	}
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_ESC);

	/* Case 2: esc only -> BOOT_KEY_NONE */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, true));
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE);

	/* Case 3: refresh + arrow down -> BOOT_KEY_DOWN_ARROW */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true));
	zassert_ok(
		emulate_keystate(KEYBOARD_ROW_DOWN, KEYBOARD_COL_DOWN, true));
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_DOWN_ARROW);

	/* Case 4: refresh + L shift -> BOOT_KEY_LEFT_SHIFT */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true));
	zassert_ok(emulate_keystate(KEYBOARD_ROW_LEFT_SHIFT,
				    KEYBOARD_COL_LEFT_SHIFT, true));
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_LEFT_SHIFT);

	/* Case 5: refresh + esc + other random key -> BOOT_KEY_NONE */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true));
	zassert_ok(emulate_keystate(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, true));
	zassert_ok(
		emulate_keystate(KEYBOARD_ROW_KEY_0, KEYBOARD_COL_KEY_0, true));
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE);

	/* Case 6: BOOT_KEY_NONE after late sysjump */
	system_jumped_late_fake.return_val = 1;
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true));
	zassert_ok(emulate_keystate(KEYBOARD_ROW_LEFT_SHIFT,
				    KEYBOARD_COL_LEFT_SHIFT, true));
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE);
}

ZTEST(keyboard_scan, test_press_enter)
{
	zassert_ok(emulate_keystate(4, 11, true));
	k_sleep(K_MSEC(100));
	/* TODO(jbettis): Check espi_emul to verify the AP was notified. */
	zassert_ok(emulate_keystate(4, 11, false));
	k_sleep(K_MSEC(100));
}

ZTEST(keyboard_scan, test_console_command_ksstate__noargs)
{
	const char *outbuffer;
	size_t buffer_size;

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for some expected lines */
	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Keyboard scan disable mask: 0x00000000"),
		   "Output was: `%s`", outbuffer);
	zassert_ok(!strstr(outbuffer, "Keyboard scan state printing off"),
		   "Output was: `%s`", outbuffer);

	/* Ensure we are still scanning */
	zassert_true(keyboard_scan_is_enabled());
}

ZTEST(keyboard_scan, test_console_command_ksstate__force)
{
	/* This command forces the keyboard to start scanning (if not already)
	 * and enable state change printing. To test: turn scanning off, run
	 * command, and verify we are scanning and printing state
	 */

	keyboard_scan_enable(false, -1);
	zassert_false(keyboard_scan_is_enabled());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate force"));

	zassert_true(keyboard_scan_is_enabled());
	zassert_true(keyboard_scan_get_print_state_changes());
}

ZTEST(keyboard_scan, test_console_command_ksstate__on_off)
{
	/* This command turns state change printing on/off */

	zassert_false(keyboard_scan_get_print_state_changes());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate on"));
	zassert_true(keyboard_scan_get_print_state_changes());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate off"));
	zassert_false(keyboard_scan_get_print_state_changes());
}

ZTEST(keyboard_scan, test_console_command_ksstate__invalid)
{
	/* Pass a string that cannot be parsed as a bool */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "ksstate xyz"));
}

ZTEST(keyboard_scan, test_console_command_kbpress__noargs)
{
	const char *outbuffer;
	size_t buffer_size;

	/* With no args, print list of simulated keys */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "kbpress"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for an expected line */
	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Simulated keys:"), "Output was: `%s`",
		   outbuffer);
}

ZTEST(keyboard_scan, test_console_command_kbpress__invalid)
{
	/* Row or column number out of range, or wrong type */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "kbpress -1 0"));
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "kbpress foo 0"));
	zassert_ok(!shell_execute_cmd(
		get_ec_shell(), "kbpress " STRINGIFY(KEYBOARD_COLS_MAX) " 0"));

	zassert_ok(!shell_execute_cmd(get_ec_shell(), "kbpress 0 -1"));
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "kbpress 0 foo"));
	zassert_ok(!shell_execute_cmd(
		get_ec_shell(), "kbpress 0 " STRINGIFY(KEYBOARD_COLS_MAX)));
}

/* Mock the key_state_changed callback that the key scan task invokes whenever
 * a key event occurs. This will capture a history of key presses.
 */
FAKE_VOID_FUNC(key_state_changed, int, int, uint8_t);

ZTEST(keyboard_scan, test_console_command_kbpress__press)
{
	/* Press and release a key */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "kbpress 1 2"));

	/* Pause a bit to allow the key scan task to process. */
	k_sleep(K_MSEC(500));

	/* Expect two key events */
	zassert_equal(2, key_state_changed_fake.call_count);

	/* Press col=1,row=2 (state==1) */
	zassert_equal(1, key_state_changed_fake.arg1_history[0]);
	zassert_equal(2, key_state_changed_fake.arg0_history[0]);
	zassert_true(key_state_changed_fake.arg2_history[0]);

	/* Release col=1,row=2 (state==0) */
	zassert_equal(1, key_state_changed_fake.arg1_history[1]);
	zassert_equal(2, key_state_changed_fake.arg0_history[1]);
	zassert_false(key_state_changed_fake.arg2_history[1]);
}

ZTEST(keyboard_scan, test_console_command_kbpress__down_and_up)
{
	/* Hold a key down */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "kbpress 3 4 1"));

	/* Release the key */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "kbpress 3 4 0"));

	/* Pause a bit to allow the key scan task to process. */
	k_sleep(K_MSEC(500));

	/* Expect two key events */
	zassert_equal(2, key_state_changed_fake.call_count,
		      "Actual call_count=%d",
		      key_state_changed_fake.call_count);

	/* Press col=3,row=4 (state==1) */
	zassert_equal(3, key_state_changed_fake.arg1_history[0]);
	zassert_equal(4, key_state_changed_fake.arg0_history[0]);
	zassert_true(key_state_changed_fake.arg2_history[0]);

	/* Release col=3,row=4 (state==0) */
	zassert_equal(3, key_state_changed_fake.arg1_history[1]);
	zassert_equal(4, key_state_changed_fake.arg0_history[1]);
	zassert_false(key_state_changed_fake.arg2_history[1]);
}

ZTEST(keyboard_scan, test_host_command_simulate_key__locked)
{
	uint16_t ret;

	zassert_true(system_is_locked(), "Expecting locked system.");

	struct ec_params_mkbp_simulate_key params;

	ret = ec_cmd_mkbp_simulate_key(NULL, &params);
	zassert_equal(EC_RES_ACCESS_DENIED, ret, "Command returned %u", ret);
}

ZTEST(keyboard_scan, test_host_command_simulate_key__bad_params)
{
	uint16_t ret;

	system_is_locked_fake.return_val = 0;
	zassert_false(system_is_locked(), "Expecting unlocked system.");

	struct ec_params_mkbp_simulate_key params = {
		.col = KEYBOARD_COLS_MAX,
		.row = KEYBOARD_ROWS,
	};

	ret = ec_cmd_mkbp_simulate_key(NULL, &params);
	zassert_equal(EC_RES_INVALID_PARAM, ret, "Command returned %u", ret);
}

/**
 * @brief Helper function that sends a host command to press or release the
 *        specified key.
 *
 * @param col Key column
 * @param row Key row
 * @param pressed 1=press, 0=release
 * @return uint16_t Host command return code.
 */
static uint16_t send_keypress_host_command(uint8_t col, uint8_t row,
					   uint8_t pressed)
{
	struct ec_params_mkbp_simulate_key params = {
		.col = col,
		.row = row,
		.pressed = pressed,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_MKBP_SIMULATE_KEY, 0, params);

	return host_command_process(&args);
}

ZTEST(keyboard_scan, test_host_command_simulate__key_press)
{
	uint16_t ret;

	system_is_locked_fake.return_val = 0;
	zassert_false(system_is_locked(), "Expecting unlocked system.");

	ret = send_keypress_host_command(1, 2, 1);
	zassert_equal(EC_RES_SUCCESS, ret, "Command returned %u", ret);

	/* Release the key */
	ret = send_keypress_host_command(1, 2, 0);
	zassert_equal(EC_RES_SUCCESS, ret, "Command returned %u", ret);

	/* Verify key events happened */

	zassert_equal(2, key_state_changed_fake.call_count);

	/* Press col=1,row=2 (state==1) */
	zassert_equal(1, key_state_changed_fake.arg1_history[0]);
	zassert_equal(2, key_state_changed_fake.arg0_history[0]);
	zassert_true(key_state_changed_fake.arg2_history[0]);

	/* Release col=1,row=2 (state==0) */
	zassert_equal(1, key_state_changed_fake.arg1_history[1]);
	zassert_equal(2, key_state_changed_fake.arg0_history[1]);
	zassert_false(key_state_changed_fake.arg2_history[1]);
}

FAKE_VOID_FUNC(system_enter_hibernate, uint32_t, uint32_t);
FAKE_VOID_FUNC(chipset_reset, int);

ZTEST(keyboard_scan, test_special_key_combos)
{
	system_is_locked_fake.return_val = 0;
	zassert_false(system_is_locked(), "Expecting unlocked system.");

	/* Set the volume up key coordinates to something arbitrary */
	int vol_up_col = 1;
	int vol_up_row = 2;

	set_vol_up_key(vol_up_row, vol_up_col);

	/* Vol up and the alt keys must be in different columns */
	zassert_false(vol_up_col == KEYBOARD_COL_LEFT_ALT, NULL);

	/* Hold down volume up, left alt (either alt key works), and R */
	zassert_ok(send_keypress_host_command(vol_up_col, vol_up_row, 1));
	zassert_ok(send_keypress_host_command(KEYBOARD_COL_LEFT_ALT,
					      KEYBOARD_ROW_LEFT_ALT, 1));
	zassert_ok(send_keypress_host_command(KEYBOARD_COL_KEY_R,
					      KEYBOARD_ROW_KEY_R, 1));

	k_sleep(K_MSEC(100));

	/* Release R and the press H */
	zassert_ok(send_keypress_host_command(KEYBOARD_COL_KEY_R,
					      KEYBOARD_ROW_KEY_R, 0));
	zassert_ok(send_keypress_host_command(KEYBOARD_COL_KEY_H,
					      KEYBOARD_ROW_KEY_H, 1));

	k_sleep(K_MSEC(100));

	/* Release all */
	zassert_ok(send_keypress_host_command(vol_up_col, vol_up_row, 0));
	zassert_ok(send_keypress_host_command(KEYBOARD_COL_LEFT_ALT,
					      KEYBOARD_ROW_LEFT_ALT, 0));
	zassert_ok(send_keypress_host_command(KEYBOARD_COL_KEY_H,
					      KEYBOARD_ROW_KEY_H, 0));

	/* Check that a reboot was requested (VOLUP + ALT + R) */
	zassert_equal(1, chipset_reset_fake.call_count,
		      "Did not try to reboot");
	zassert_equal(CHIPSET_RESET_KB_WARM_REBOOT,
		      chipset_reset_fake.arg0_history[0]);

	/* Check that we called system_enter_hibernate (VOLUP + ALT + H) */
	zassert_equal(1, system_enter_hibernate_fake.call_count,
		      "Did not enter hibernate");
}

static void reset_keyboard(void *data)
{
	ARG_UNUSED(data);

	/* Enable scanning and clear all reason bits (reason bits explain why
	 * scanning was disabled -- see `enum kb_scan_disable_masks`)
	 */
	keyboard_scan_enable(true, -1);

	/* Turn off key state change printing */
	keyboard_scan_set_print_state_changes(0);

	/* Reset KB emulator */
	clear_emulated_keys();

	/* Clear debouncing state to prevent latent key presses from appearing
	 * in a later test.
	 */
	test_keyboard_scan_debounce_reset();

	/* Reset all mocks. */
	RESET_FAKE(key_state_changed);
	RESET_FAKE(system_is_locked);
	RESET_FAKE(system_enter_hibernate);
	RESET_FAKE(chipset_reset);

	/* Be locked by default */
	system_is_locked_fake.return_val = 1;
}

ZTEST_SUITE(keyboard_scan, drivers_predicate_post_main, NULL, reset_keyboard,
	    reset_keyboard, NULL);

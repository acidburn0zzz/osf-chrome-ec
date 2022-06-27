/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include "include/lpc.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

struct host_cmd_host_event_commands_fixture {
	host_event_t lpc_host_events;
	host_event_t lpc_host_event_mask[LPC_HOST_EVENT_COUNT];
};

static void *host_cmd_host_event_commands_setup(void)
{
	static struct host_cmd_host_event_commands_fixture fixture = { 0 };

	return &fixture;
}

static void host_cmd_host_event_commands_before(void *fixture)
{
	struct host_cmd_host_event_commands_fixture *f = fixture;

	f->lpc_host_events = lpc_get_host_events();

	for (int i = 0; i < LPC_HOST_EVENT_COUNT; i++) {
		f->lpc_host_event_mask[i] = lpc_get_host_events_by_type(i);
	}
}

static void host_cmd_host_event_commands_after(void *fixture)
{
	struct host_cmd_host_event_commands_fixture *f = fixture;

	lpc_set_host_event_state(f->lpc_host_events);

	for (int i = 0; i < LPC_HOST_EVENT_COUNT; i++) {
		lpc_set_host_event_mask(i, f->lpc_host_event_mask[i]);
	}
}

ZTEST_SUITE(host_cmd_host_event_commands, drivers_predicate_post_main,
	    host_cmd_host_event_commands_setup,
	    host_cmd_host_event_commands_before,
	    host_cmd_host_event_commands_after, NULL);

static enum ec_status host_event_cmd_helper(enum ec_host_event_action action,
					    uint8_t mask,
					    struct ec_response_host_event *r)
{
	enum ec_status ret_val;

	struct ec_params_host_event params = {
		.action = action,
		.mask_type = mask,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_EVENT, 0, *r, params);

	ret_val = host_command_process(&args);

	return ret_val;
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT invalid host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_invalid_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };

	ret_val = host_event_cmd_helper(0xFF, 0, &result);

	zassert_equal(ret_val, EC_RES_INVALID_PARAM, "Expected=%d, returned=%d",
		      EC_RES_INVALID_PARAM, ret_val);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT get host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };
	struct {
		uint8_t mask;
		enum ec_status result;
	} event_get[] = {
		{ EC_HOST_EVENT_MAIN, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_B, EC_RES_SUCCESS },
#ifdef CONFIG_HOSTCMD_X86
		{ EC_HOST_EVENT_SCI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_SMI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ALWAYS_REPORT_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ACTIVE_WAKE_MASK, EC_RES_SUCCESS },
#ifdef CONFIG_POWER_S0IX
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX, EC_RES_SUCCESS },
#endif /* CONFIG_POWER_S0IX */
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S3, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S5, EC_RES_SUCCESS },
#endif /* CONFIG_HOSTCMD_X86 */
		{ 0xFF, EC_RES_INVALID_PARAM },
	};

	for (int i = 0; i < ARRAY_SIZE(event_get); i++) {
		ret_val = host_event_cmd_helper(EC_HOST_EVENT_GET,
						event_get[i].mask, &result);
		zassert_equal(ret_val, event_get[i].result,
			      "[%d] Expected=%d, returned=%d", i,
			      event_get[i].result, ret_val);
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT set host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };
	struct {
		uint8_t mask;
		enum ec_status result;
	} event_set[] = {
		{ EC_HOST_EVENT_MAIN, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_B, EC_RES_ACCESS_DENIED },
#ifdef CONFIG_HOSTCMD_X86
		{ EC_HOST_EVENT_SCI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_SMI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ALWAYS_REPORT_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ACTIVE_WAKE_MASK, EC_RES_SUCCESS },
#ifdef CONFIG_POWER_S0IX
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX, EC_RES_SUCCESS },
#endif /* CONFIG_POWER_S0IX */
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S3, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S5, EC_RES_SUCCESS },
#endif /* CONFIG_HOSTCMD_X86 */
		{ 0xFF, EC_RES_INVALID_PARAM },
	};

	for (int i = 0; i < ARRAY_SIZE(event_set); i++) {
		ret_val = host_event_cmd_helper(EC_HOST_EVENT_SET,
						event_set[i].mask, &result);
		zassert_equal(ret_val, event_set[i].result,
			      "[%d] Expected=%d, returned=%d", i,
			      event_set[i].result, ret_val);
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT clear host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_clear_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };
	struct {
		uint8_t mask;
		enum ec_status result;
	} event_set[] = {
		{ EC_HOST_EVENT_MAIN, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_B, EC_RES_SUCCESS },
#ifdef CONFIG_HOSTCMD_X86
		{ EC_HOST_EVENT_SCI_MASK, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_SMI_MASK, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_ALWAYS_REPORT_MASK, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_ACTIVE_WAKE_MASK, EC_RES_ACCESS_DENIED },
#ifdef CONFIG_POWER_S0IX
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX, EC_RES_ACCESS_DENIED },
#endif /* CONFIG_POWER_S0IX */
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S3, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S5, EC_RES_ACCESS_DENIED },
#endif /* CONFIG_HOSTCMD_X86 */
		{ 0xFF, EC_RES_INVALID_PARAM },
	};

	for (int i = 0; i < ARRAY_SIZE(event_set); i++) {
		ret_val = host_event_cmd_helper(EC_HOST_EVENT_CLEAR,
						event_set[i].mask, &result);
		zassert_equal(ret_val, event_set[i].result,
			      "Expected [%d] result=%d, returned=%d", i,
			      event_set[i].result, ret_val);
	}
}

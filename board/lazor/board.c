/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lazor board-specific configuration */

#include "adc_chip.h"
#include "battery_fuel_gauge.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "extpower.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/ln9310.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "pi3usb9201.h"
#include "power.h"
#include "power/sc7180.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"
#include "shi_chip.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* Forward declaration */
static void tcpc_alert_event(enum gpio_signal signal);
static void usb0_evt(enum gpio_signal signal);
static void usb1_evt(enum gpio_signal signal);
static void usba_oc_interrupt(enum gpio_signal signal);
static void ppc_interrupt(enum gpio_signal signal);
static void board_connect_c0_sbu(enum gpio_signal s);

#include "gpio_list.h"

/* GPIO Interrupt Handlers */
static void tcpc_alert_event(enum gpio_signal signal)
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

static void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
}

static void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12);
}

static void usba_oc_deferred(void)
{
	/* Use next number after all USB-C ports to indicate the USB-A port */
	board_overcurrent_event(CONFIG_USB_PD_PORT_MAX_COUNT,
				!gpio_get_level(GPIO_USB_A0_OC_ODL));
}
DECLARE_DEFERRED(usba_oc_deferred);

static void usba_oc_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&usba_oc_deferred_data, 0);
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_SWCTL_INT_ODL:
		sn5s330_interrupt(0);
		break;
	case GPIO_USB_C1_SWCTL_INT_ODL:
		sn5s330_interrupt(1);
		break;
	default:
		break;
	}
}

static void board_connect_c0_sbu_deferred(void)
{
	/*
	 * If CCD_MODE_ODL asserts, it means there's a debug accessory connected
	 * and we should enable the SBU FETs.
	 */
	ppc_set_sbu(0, 1);
}
DECLARE_DEFERRED(board_connect_c0_sbu_deferred);

static void board_connect_c0_sbu(enum gpio_signal s)
{
	hook_call_deferred(&board_connect_c0_sbu_deferred_data, 0);
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/* Use 80 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/*
	 * Unmask 0x08 in [0] (KSO_00/KSI_03, the new location of Search key);
	 * as it still uses the legacy location (KSO_01/KSI_00).
	 */
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca
	},
	/* Other values should be the same as the default configuration. */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
};

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  100, GPIO_EC_I2C_POWER_SCL,
					  GPIO_EC_I2C_POWER_SDA},
	{"tcpc0",   I2C_PORT_TCPC0, 1000, GPIO_EC_I2C_USB_C0_PD_SCL,
					  GPIO_EC_I2C_USB_C0_PD_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_EC_I2C_USB_C1_PD_SCL,
					  GPIO_EC_I2C_USB_C1_PD_SDA},
	{"eeprom",  I2C_PORT_EEPROM, 400, GPIO_EC_I2C_EEPROM_SCL,
					  GPIO_EC_I2C_EEPROM_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_EC_I2C_SENSOR_SCL,
					  GPIO_EC_I2C_SENSOR_SDA},
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Measure VBUS through a 1/10 voltage divider */
	[ADC_VBUS] = {
		"VBUS",
		NPCX_ADC_CH1,
		ADC_MAX_VOLT * 10,
		ADC_READ_MAX + 1,
		0
	},
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = {
		"AMON_BMON",
		NPCX_ADC_CH2,
		ADC_MAX_VOLT * 1000 / 18,
		ADC_READ_MAX + 1,
		0
	},
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 5.6K resistor, to read
	 * 0.8V @ 99 W, i.e. 124000 uW/mV. Using ADC_MAX_VOLT*124000 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = {
		"PSYS",
		NPCX_ADC_CH3,
		ADC_MAX_VOLT * 124000 * 2 / (ADC_READ_MAX + 1),
		2,
		0
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 10000 },
	/* TODO(waihong): Assign a proper frequency. */
	[PWM_CH_DISPLIGHT] = { .channel = 5, .flags = 0, .freq = 4800 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Power Path Controller */
struct ppc_config_t ppc_chips[] = {
	{
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv
	},
	{
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = PS8751_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8751_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

/*
 * Port-0/1 USB mux driver.
 *
 * The USB mux is handled by TCPC chip and the HPD update is through a GPIO
 * to AP. But the TCPC chip is also needed to know the HPD status; otherwise,
 * the mux misbehaves.
 */
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
	{
		.usb_port = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

/* BC1.2 */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_POWER,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	{
		.i2c_port = I2C_PORT_EEPROM,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/* Mutexes */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;
static struct icm_drv_data_t g_icm426xx_data;
static struct accelgyro_saved_data_t g_bma255_data;

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI160 = 1,
	BASE_GYRO_ICM426XX = 2,
};

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t base_standard_ref_bmi160 = {
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0,  FLOAT_TO_FP(-1),  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

const mat33_fp_t base_standard_ref_icm426xx = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

static const mat33_fp_t lid_standard_ref_bma255 = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

static const mat33_fp_t lid_standard_ref_kx022 = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMA255,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bma2x2_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bma255_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
	 .rot_standard_ref = &lid_standard_ref_bma255,
	 .default_range = 2, /* g, to support lid angle calculation. */
	 .min_frequency = BMA255_ACCEL_MIN_FREQ,
	 .max_frequency = BMA255_ACCEL_MAX_FREQ,
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = &base_standard_ref_bmi160,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	[BASE_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref_bmi160,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t kx022_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3_S5,
	.chip = MOTIONSENSE_CHIP_KX022,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &kionix_accel_drv,
	.mutex = &g_lid_mutex,
	.drv_data = &g_kx022_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = KX022_ADDR0_FLAGS,
	.rot_standard_ref = &lid_standard_ref_kx022,
	.default_range = 2, /* g, enough for laptop. */
	.min_frequency = KX022_ACCEL_MIN_FREQ,
	.max_frequency = KX022_ACCEL_MAX_FREQ,
	.config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		 },
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3_S5,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	.rot_standard_ref = &base_standard_ref_icm426xx,
	.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3_S5,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_standard_ref_icm426xx,
	.min_frequency = ICM426XX_GYRO_MIN_FREQ,
	.max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
#endif

__override int board_get_default_battery_type(void)
{
	/*
	 * A 2S battery is set as default. If the board is configured to use
	 * a 3S battery, according to its SKU_ID, return a 3S battery as
	 * default. It helps to configure the charger to output a correct
	 * voltage in case the battery is not attached.
	 */
	if (board_get_battery_cell_type() == BATTERY_CELL_TYPE_3S)
		return BATTERY_LGC_AP18C8K;

	return DEFAULT_BATTERY_TYPE;
}

static int base_accelgyro_config;

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_accelgyro_config) {
	case BASE_GYRO_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	case BASE_GYRO_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
}

static void board_detect_motionsensor(void)
{
	int ret;
	int val;

	/* Check lid accel chip */
	ret = i2c_read8(I2C_PORT_SENSOR, BMA2x2_I2C_ADDR1_FLAGS,
		BMA2x2_CHIP_ID_ADDR, &val);
	if (ret)
		motion_sensors[LID_ACCEL] = kx022_lid_accel;

	CPRINTS("Lid Accel: %s", ret ? "KX022" : "BMA255");

	/* Check base accelgyro chip */
	ret = icm_read8(&icm426xx_base_accel, ICM426XX_REG_WHO_AM_I, &val);
	if (val == ICM426XX_CHIP_ICM40608) {
		motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
		motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
	}

	base_accelgyro_config = (val == ICM426XX_CHIP_ICM40608)
		 ? BASE_GYRO_ICM426XX : BASE_GYRO_BMI160;
	CPRINTS("Base Accelgyro: %s", (val == ICM426XX_CHIP_ICM40608)
		 ? "ICM40608" : "BMI160");
}

static void board_update_sensor_config_from_sku(void)
{
	if (board_is_clamshell()) {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* The sensors are not stuffed; don't allow lines to float */
		gpio_set_flags(GPIO_ACCEL_GYRO_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_LID_ACCEL_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	} else {
		board_detect_motionsensor();
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable interrupt for the base accel sensor */
		gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);
	}
}
DECLARE_HOOK(HOOK_INIT, board_update_sensor_config_from_sku,
	     HOOK_PRIO_INIT_I2C + 2);

/* Initialize board. */
static void board_init(void)
{
	/* Enable BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable USB-A overcurrent interrupt */
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);

	/*
	 * The H1 SBU line for CCD are behind PPC chip. The PPC internal FETs
	 * for SBU may be disconnected after DP alt mode is off. Should enable
	 * the CCD_MODE_ODL interrupt to make sure the SBU FETs are connected.
	 */
	gpio_enable_interrupt(GPIO_CCD_MODE_ODL);

	/* Set the backlight duty cycle to 0. AP will override it later. */
	pwm_set_duty(PWM_CH_DISPLIGHT, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	int i;

	if (!board_is_clamshell()) {
		/*
		 * Sensors are unpowered in hibernate. Apply PD to the
		 * interrupt lines such that they don't float.
		 */
		gpio_set_flags(GPIO_ACCEL_GYRO_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_LID_ACCEL_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}

	/*
	 * Board rev 5+ has the hardware fix. Don't need the following
	 * workaround.
	 */
	if (system_get_board_version() >= 5)
		return;

	/*
	 * Enable the PPC power sink path before EC enters hibernate;
	 * otherwise, ACOK won't go High and can't wake EC up. Check the
	 * bug b/170324206 for details.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		ppc_vbus_sink_enable(i, 1);
}

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		board_reset_pd_mcu();
	}

	/* Enable PPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_SWCTL_INT_ODL);

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, 0, 0);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C+1);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/*
	 * Turn off display backlight in S3. AP has its own control. The EC's
	 * and the AP's will be AND'ed together in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
	pwm_enable(PWM_CH_DISPLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Turn on display and keyboard backlight in S0. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
	if (pwm_get_duty(PWM_CH_DISPLIGHT))
		pwm_enable(PWM_CH_DISPLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

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


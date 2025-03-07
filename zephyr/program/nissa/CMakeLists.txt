# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories(include)
zephyr_library_sources("src/common.c")
zephyr_library_sources_ifdef(CONFIG_NISSA_SUB_BOARD "src/sub_board.c")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

if(DEFINED CONFIG_BOARD_NIVVIKS)
	project(nivviks)
	zephyr_library_sources(
			"nivviks/src/led.c"
			"nivviks/src/form_factor.c"
			"nivviks/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "nivviks/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nivviks/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nivviks/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NEREID OR DEFINED CONFIG_BOARD_NEREID_CX)
	project(nereid)
	zephyr_library_sources(
			"src/led.c"
			"nereid/src/keyboard.c"
			"nereid/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nereid/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nereid/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASK)
	zephyr_library_sources(
			"craask/src/fan.c"
			"craask/src/form_factor.c"
			"craask/src/keyboard.c"
			"craask/src/kb_backlight.c"
			"craask/src/led.c"
			"craask/src/thermal.c"
	)
	project(craask)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "craask/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "craask/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJO)
	project(pujjo)
	zephyr_library_sources(
			"pujjo/src/led.c"
			"pujjo/src/keyboard.c"
			"pujjo/src/hdmi.c"
			"pujjo/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "pujjo/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjo/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "pujjo/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_XIVU)
	project(xivu)
	zephyr_library_sources(
			"xivu/src/form_factor.c"
			"xivu/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "xivu/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "xivu/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_JOXER)
	project(joxer)
	zephyr_library_sources(
			"joxer/src/led.c"
			"joxer/src/keyboard.c"
			"joxer/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "joxer/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "joxer/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "joxer/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVIKS)
	project(yaviks)
	zephyr_library_sources(
			"yaviks/src/led.c"
			"yaviks/src/keyboard.c"
			"yaviks/src/board.c"
			"yaviks/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "yaviks/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "yaviks/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "yaviks/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVILLA)
	project(yavilla)
	zephyr_library_sources(
			"yavilla/src/led.c"
			"yavilla/src/keyboard.c"
			"yavilla/src/board.c"
			"yavilla/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "yavilla/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "yavilla/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "yavilla/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_XIVUR)
	project(xivur)
	zephyr_library_sources(
			"xivur/src/led.c"
			"xivur/src/board.c"
			"xivur/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "xivur/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "xivur/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "xivur/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_ULDREN)
	project(uldren)
	zephyr_library_sources(
			"uldren/src/form_factor.c"
			"uldren/src/kb_backlight.c"
			"uldren/src/sub_board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "uldren/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "uldren/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_GOTHRAX)
	project(gothrax)
	zephyr_library_sources(
			"src/led.c"
			"gothrax/src/keyboard.c"
			"gothrax/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "gothrax/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "gothrax/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "gothrax/src/fan.c")
endif()

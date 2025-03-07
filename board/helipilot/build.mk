# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Helipilot board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m8s

board-y=
board-y+=board.o
board-rw=board_rw.o

# If we're mocking the sensor detection for testing (so we can test
# sensor/transport permutations in the unit tests), don't build the real sensor
# detection.
ifeq ($(HAS_MOCK_FPSENSOR_DETECT),)
	board-y+=fpsensor_detect.o
	board-rw+=fpsensor_detect_rw.o
endif

# Do not build rsa test because this board uses RSA exponent 3 and the rsa test
# will fail on device.
test-list-y=\
       abort \
       aes \
       always_memset \
       benchmark \
       cec \
       compile_time_macros \
       cortexm_fpu \
       crc \
       debug \
       exception \
       flash_physical \
       flash_write_protect \
       fpsensor \
       fpsensor_hw \
       ftrapv \
       global_initialization \
       libc_printf \
       libcxx \
       malloc \
       mpu \
       mutex \
       panic \
       panic_data \
       pingpong \
       printf \
       queue \
       rng_benchmark \
       rollback \
       rollback_entropy \
       rsa3 \
       rtc \
       sbrk \
       scratchpad \
       sha256 \
       sha256_unrolled \
       static_if \
       stdlib \
       std_vector \
       stm32f_rtc \
       system_is_locked \
       timer \
       timer_dos \
       tpm_seed_clear \
       utils \
       utils_str \

# Note that this variable includes the trailing "/"
_hatch_fp_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))
-include $(_hatch_fp_cur_dir)../../private/board/hatch_fp/build.mk

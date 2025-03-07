/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "charge_manager.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

int pd_snk_is_vbus_provided(int port)
{
	static atomic_t vbus_prev[CONFIG_USB_PD_PORT_MAX_COUNT];
	int vbus;

	/*
	 * (b:181203590#comment20) TODO(yllin): use
	 *  PD_VSINK_DISCONNECT_PD for non-5V case.
	 */
	vbus = adc_read_channel(board_get_vbus_adc(port)) >=
	       PD_V_SINK_DISCONNECT_MAX;

#ifdef CONFIG_USB_CHARGER
	/*
	 * There's no PPC to inform VBUS change for usb_charger, so inform
	 * the usb_charger now.
	 */
	if (!!(vbus_prev[port] != vbus)) {
		usb_charger_vbus_change(port, vbus);
	}

	if (vbus) {
		atomic_or(&vbus_prev[port], 1);
	} else {
		atomic_clear(&vbus_prev[port]);
	}
#endif
	return vbus;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

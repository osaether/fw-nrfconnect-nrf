/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: BSD-5-Clause-Nordic
 */

#include <limits.h>

#include <zephyr/types.h>

#include "button_event.h"
#include "motion_event.h"
#include "power_event.h"
#include "hid_event.h"
#include "ble_event.h"

#define MODULE hid_state
#include "module_state_event.h"

#define SYS_LOG_DOMAIN	MODULE_NAME
#define SYS_LOG_LEVEL	CONFIG_DESKTOP_SYS_LOG_HID_STATE_LEVEL
#include <logging/sys_log.h>


static bool connected;

static s16_t acc_x;
static s16_t acc_y;

static bool keys[CONFIG_DESKTOP_KEY_STATE_SIZE];
static u32_t buttons;

static void send_report_keyboard(void)
{
	if (IS_ENABLED(CONFIG_DESKTOP_HID_KEYBOARD)) {
		struct hid_keyboard_event *event = new_hid_keyboard_event();

		if (!event) {
			SYS_LOG_WRN("Failed to allocate an event");
			return;
		}

		size_t cnt = 0;

		for (size_t i = 0;
		     (i < ARRAY_SIZE(keys)) && (cnt < ARRAY_SIZE(event->keys));
		     i++) {
			if (keys[i]) {
				event->keys[cnt] = 0x04 + i;
				cnt++;
			}
		}
		for (; cnt < ARRAY_SIZE(event->keys); cnt++) {
			event->keys[cnt] = 0;
		}

		event->modifier_bm = 0;

		EVENT_SUBMIT(event);
	}
}

static void send_report_mouse_xy(void)
{
	if (IS_ENABLED(CONFIG_DESKTOP_HID_MOUSE)) {
		struct hid_mouse_xy_event *event = new_hid_mouse_xy_event();

		if (!event) {
			SYS_LOG_WRN("Failed to allocate an event");
			return;
		}

		if (acc_x > SCHAR_MAX) {
			event->dx = SCHAR_MAX;
		} else {
			event->dx = acc_x;
		}

		if (acc_y > SCHAR_MAX) {
			event->dy = SCHAR_MAX;
		} else {
			event->dy = acc_y;
		}

		acc_x -= event->dx;
		acc_y -= event->dy;

		EVENT_SUBMIT(event);
	}
}

static void send_report_mouse_buttons(void)
{
	if (IS_ENABLED(CONFIG_DESKTOP_HID_MOUSE)) {
		struct hid_mouse_button_event *event = new_hid_mouse_button_event();

		if (!event) {
			SYS_LOG_WRN("Failed to allocate an event");
			return;
		}

		event->button_bm = buttons;

		EVENT_SUBMIT(event);
	}
}

static void keep_device_active(void)
{
	struct keep_active_event *event = new_keep_active_event();

	if (event) {
		event->module_name = MODULE_NAME;
		EVENT_SUBMIT(event);
	}
}

static bool event_handler(const struct event_header *eh)
{
	if (is_motion_event(eh)) {
		struct motion_event *event = cast_motion_event(eh);

		SYS_LOG_INF("motion event");

		s32_t new_x = acc_x + event->dx;
		s32_t new_y = acc_y + event->dy;

		/* TODO reqs say we should not accumulate */
		if (new_x > SHRT_MAX) {
			acc_x = SHRT_MAX;
		} else if (new_x < SHRT_MIN) {
			acc_x = SHRT_MIN;
		} else {
			acc_x = new_x;
		}

		if (new_y > SHRT_MAX) {
			acc_y = SHRT_MAX;
		} else if (new_y < SHRT_MIN) {
			acc_y = SHRT_MIN;
		} else {
			acc_y = new_y;
		}

		if (connected) {
			send_report_mouse_xy();
		}

		keep_device_active();

		return false;
	}

	if (is_button_event(eh)) {
		struct button_event *event = cast_button_event(eh);

		SYS_LOG_INF("button event");

		if (event->key_id < ARRAY_SIZE(keys)) {
			keys[event->key_id] = event->pressed;

			if (connected) {
				send_report_keyboard();
			}
		} else if ((event->key_id >= 0x20) && (event->key_id < 0x25)) {
			u32_t mask = 1 << (event->key_id - 0x20);
			if (event->pressed) {
				buttons |= mask;
			} else {
				buttons &= ~mask;
			}
			if (connected) {
				send_report_mouse_buttons();
			}
		}

		keep_device_active();

		return false;
	}

	if (is_ble_peer_event(eh)) {
		struct ble_peer_event *event = cast_ble_peer_event(eh);

		SYS_LOG_INF("peer %sconnected",
				(event->connected)?(""):("dis"));

		connected = event->connected;
		if (connected) {
			send_report_keyboard();
			send_report_mouse_buttons();
		}

		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}
EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, ble_peer_event);
EVENT_SUBSCRIBE(MODULE, button_event);
EVENT_SUBSCRIBE(MODULE, motion_event);

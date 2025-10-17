/*
 * Copyright (c) 2018 qianfan Zhao
 * Copyright (c) 2018, 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include "hid_gamepad.h"

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// static const uint8_t hid_report_desc[] = HID_MOUSE_REPORT_DESC(2);
static enum usb_dc_status_code usb_status;

K_MSGQ_DEFINE(gamepad_msgq, sizeof(gamepad_report_t), 2, 1);
static K_SEM_DEFINE(ep_write_sem, 0, 1);

static inline void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    usb_status = status;
}

static ALWAYS_INLINE void rwup_if_suspended(void)
{
    if (IS_ENABLED(CONFIG_USB_DEVICE_REMOTE_WAKEUP)) {
        if (usb_status == USB_DC_SUSPEND) {
            usb_wakeup_request();
            return;
        }
    }
}

/// Standard Gamepad HAT/DPAD Buttons (from Linux input event codes)
typedef enum
{
  GAMEPAD_HAT_CENTERED   = 0,  ///< DPAD_CENTERED
  GAMEPAD_HAT_UP         = 1,  ///< DPAD_UP
  GAMEPAD_HAT_UP_RIGHT   = 2,  ///< DPAD_UP_RIGHT
  GAMEPAD_HAT_RIGHT      = 3,  ///< DPAD_RIGHT
  GAMEPAD_HAT_DOWN_RIGHT = 4,  ///< DPAD_DOWN_RIGHT
  GAMEPAD_HAT_DOWN       = 5,  ///< DPAD_DOWN
  GAMEPAD_HAT_DOWN_LEFT  = 6,  ///< DPAD_DOWN_LEFT
  GAMEPAD_HAT_LEFT       = 7,  ///< DPAD_LEFT
  GAMEPAD_HAT_UP_LEFT    = 8,  ///< DPAD_UP_LEFT
} hid_gamepad_hat_t;

static ALWAYS_INLINE hid_gamepad_hat_t convert_hat_switch(bool up, bool down, bool left, bool right)
{
    if (!(up || down || left || right)) {
        return GAMEPAD_HAT_CENTERED;
    }
    else if (up) {
        if (left) {
            return GAMEPAD_HAT_UP_LEFT;
        } else if (right) {
            return GAMEPAD_HAT_UP_RIGHT;
        } else {
            return GAMEPAD_HAT_UP;
        }
    } else if (down) {
        if (left) {
            return GAMEPAD_HAT_DOWN_LEFT;
        } else if (right) {
            return GAMEPAD_HAT_DOWN_RIGHT;
        } else {
            return GAMEPAD_HAT_DOWN;
        }
    } else {
        return (left) ? GAMEPAD_HAT_LEFT : GAMEPAD_HAT_RIGHT;
    }
}

static void input_cb(struct input_event *evt, void *user_data)
{
    static gamepad_report_t report = { 0, 0, 0 };
    static bool up = false;
    static bool down = false;
    static bool left = false;
    static bool right = false;
    static uint32_t button = 0;
    uint32_t bit = 0;

    ARG_UNUSED(user_data);

    report.report_id = 3;
    switch (evt->code) {
    case INPUT_BTN_DPAD_UP:
        rwup_if_suspended();
        up = evt->value == 1;
        break;
    case INPUT_BTN_DPAD_DOWN:
        rwup_if_suspended();
        down = evt->value == 1;
        break;
    case INPUT_BTN_DPAD_LEFT:
        rwup_if_suspended();
        left = evt->value == 1;
        break;
    case INPUT_BTN_DPAD_RIGHT:
        rwup_if_suspended();
        right = evt->value == 1;
        break;
    default:
        if ((evt->code >= INPUT_BTN_0) && (evt->code <= INPUT_BTN_9)) {
            rwup_if_suspended();
            bit = evt->code - INPUT_BTN_0;
            button = (evt-> value == 1) ? (button | (1 << bit)) : (button & ~((1 << bit)));
            report.button = button;
            break;
        } else {
            LOG_INF("Unrecognized input code %u value %d",
                evt->code, evt->value);
            return;
        }
    }

    report.x = right ? 0x7F : (left ? 0x81 : 0x00);
    report.y = down ? 0x7F : (up ? 0x81 : 0x00);
    report.hat_switch = convert_hat_switch(up, down, left, right);
    if (k_msgq_put(&gamepad_msgq, &report, K_NO_WAIT) != 0) {
        LOG_ERR("Failed to put new input event");
    }
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
static int enable_usb_device_next(void)
{
    struct usbd_context *sample_usbd;
    int err;

    sample_usbd = sample_usbd_init_device(NULL);
    if (sample_usbd == NULL) {
        LOG_ERR("Failed to initialize USB device");
        return -ENODEV;
    }

    err = usbd_enable(sample_usbd);
    if (err) {
        LOG_ERR("Failed to enable device support");
        return err;
    }

    LOG_DBG("USB device support enabled");

    return 0;
}
#endif /* defined(CONFIG_USB_DEVICE_STACK_NEXT) */

static void int_in_ready_cb(const struct device *dev)
{
    ARG_UNUSED(dev);
    k_sem_give(&ep_write_sem);
}

static const struct hid_ops ops = {
    .int_in_ready = int_in_ready_cb,
};

int main(void)
{
    const struct device *hid_dev;
    int ret;

    if (!gpio_is_ready_dt(&led0)) {
        LOG_ERR("LED device %s is not ready", led0.port->name);
        return 0;
    }

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
    hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
#else
    hid_dev = device_get_binding("HID_0");
#endif
    if (hid_dev == NULL) {
        LOG_ERR("Cannot get USB HID Device");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure the LED pin, error: %d", ret);
        return 0;
    }

    usb_hid_register_device(hid_dev,
                hid_report_desc, hid_report_desc_size,
                &ops);

    usb_hid_init(hid_dev);

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
    ret = enable_usb_device_next();
#else
    ret = usb_enable(status_cb);
#endif
    if (ret != 0) {
        LOG_ERR("Failed to enable USB");
        return 0;
    }

    while (true) {
        gamepad_report_t report;

        k_msgq_get(&gamepad_msgq, &report, K_FOREVER);

        ret = hid_int_ep_write(hid_dev, (uint8_t*)&report, sizeof(report), NULL);
        if (ret) {
            LOG_ERR("HID write error, %d", ret);
        } else {
            k_sem_take(&ep_write_sem, K_FOREVER);
            /* Toggle LED on sent report */
            (void)gpio_pin_toggle(led0.port, led0.pin);
        }
    }
    return 0;
}

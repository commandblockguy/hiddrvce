#include <string.h>
#include <debug.h>
#include "hid.h"

/* Internal functions declaration */
static usb_error_t hid_ReportCallback(usb_endpoint_t pEndpoint, usb_transfer_status_t status,
                                      size_t size, hid_state_t *hid);
static bool hid_IsKeyDownInReport(hid_keyboard_report_t *report, uint8_t key_code);
usb_error_t hid_SetProtocol(hid_state_t *hid, bool report);

static usb_error_t hid_ReportCallback(usb_endpoint_t pEndpoint, usb_transfer_status_t status,
                               size_t size, hid_state_t *hid) {

    if(status) {
        dbg_sprintf(dbgout, "callback called with status %u\n", status);
        if(status & USB_TRANSFER_NO_DEVICE) {
            hid->active = false;
            hid->stopped = true;
            if(hid->callback)
                hid->callback(hid, HID_EVENT_DISCONNECTED, 0, hid->callback_data);
            return USB_SUCCESS;
        }
    }
    if(!hid->active) {
        hid->stopped = true;
        return USB_SUCCESS;
    }
    if(hid->type == HID_KEYBOARD) {
        if(hid->callback) {
            uint8_t i;

            /* Check for keydowns */
            for(i = 0; i < sizeof(hid->report.kb.pressed); i++) {
                uint8_t key = hid->report.kb.pressed[i];
                if(!key || key == 1) continue;
                if(!hid_IsKeyDownInReport(&hid->last_report.kb, key)) {
                    hid->callback(hid, HID_EVENT_KEY_DOWN, key, hid->callback_data);
                }
            }

            /* Check for keyups */
            for(i = 0; i < sizeof(hid->last_report.kb.pressed); i++) {
                uint8_t key = hid->last_report.kb.pressed[i];
                if(!key) continue;
                if(!hid_IsKeyDownInReport(&hid->report.kb, key)) {
                    hid->callback(hid, HID_EVENT_KEY_UP, key, hid->callback_data);
                }
            }

            /* Check for modifiers */
            for(i = 0; i < 8; i++) {
                uint8_t mod = 1 << i;
                if(hid->report.kb.modifiers & mod) {
                    if(!(hid->last_report.kb.modifiers & mod)) {
                        hid->callback(hid, HID_EVENT_MODIFIER_DOWN, mod, hid->callback_data);
                    }
                } else {
                    if(hid->last_report.kb.modifiers & mod) {
                        hid->callback(hid, HID_EVENT_MODIFIER_UP, mod, hid->callback_data);
                    }
                }
            }
        }
    } else {
        uint8_t i;
        hid->delta_x += hid->report.mouse.x;
        hid->delta_y += hid->report.mouse.y;

        if(hid->callback) {
            if(hid->report.mouse.x || hid->report.mouse.y) {
                hid->callback(hid, HID_EVENT_MOUSE_MOVE, 0, hid->callback_data);
            }

            /* Check mouse buttons */
            for(i = 0; i < 8; i++) {
                uint8_t button = 1 << i;
                if(hid->report.kb.modifiers & button) {
                    if(!(hid->last_report.kb.modifiers & button)) {
                        hid->callback(hid, HID_EVENT_MOUSE_DOWN, button, hid->callback_data);
                    }
                } else {
                    if(hid->last_report.kb.modifiers & button) {
                        hid->callback(hid, HID_EVENT_MOUSE_UP, button, hid->callback_data);
                    }
                }
            }
        }
    }

    memcpy(&hid->last_report, &hid->report, sizeof(hid->report));

    usb_ScheduleTransfer(pEndpoint, &hid->report, hid->report_size,
                         (usb_transfer_callback_t)hid_ReportCallback, hid);
    return USB_SUCCESS;
}

usb_error_t hid_Init(hid_state_t *hid) {
    usb_error_t error;
    hid->delta_x = 0;
    hid->delta_y = 0;
    hid->report_size = sizeof(hid->report);
    memset(&hid->report, 0, sizeof(hid->report));
    memset(&hid->last_report, 0, sizeof(hid->last_report));
    error = hid_SetProtocol(hid, 0);
    if(error) {
        dbg_sprintf(dbgout, "error %u on protocol set\n", error);
        return error;
    }
    hid_SetIdle(hid, 1);
    if(hid->in) {
        error = usb_ScheduleTransfer(hid->in, &hid->report, hid->report_size,
                                     (usb_transfer_callback_t)hid_ReportCallback, hid);
        if(error) {
            dbg_sprintf(dbgout, "error %u on initial schedule\n", error);
            return error;
        }
    }
    hid->active = true;
    return USB_SUCCESS;
}

void hid_Stop(hid_state_t *hid) {
    if(!hid->active) return;
    hid->stopped = false;
    hid->active = false;
    while(!hid->stopped) usb_WaitForEvents();
}

usb_error_t hid_SetProtocol(hid_state_t *hid, bool report) {
    usb_control_setup_t setup = {0x21, 0x0B, 0, 0, 0};
    setup.wIndex = hid->interface;
    setup.wValue = report;
    return usb_DefaultControlTransfer(hid->dev, &setup, NULL, 50, NULL);
}

usb_error_t hid_SetIdle(hid_state_t *hid, uint8_t time) {
    usb_control_setup_t setup = {0x21, 0x0A, 0, 0, 0};

    setup.wIndex = hid->interface;
    setup.wValue = time << 8;

    return usb_DefaultControlTransfer(hid->dev, &setup, NULL, 50, NULL);
}

usb_descriptor_t *hid_GetNext(usb_descriptor_t *search_pos, usb_descriptor_t *end,
                              hid_state_t *result, usb_device_t dev) {
    bool interface_found = false;

    result->dev = dev;
    result->active = false;
    result->in = NULL;
    result->out = NULL;
    result->interface = -1;
    result->type = 0;
    result->callback = NULL;
    result->callback_data = NULL;

    while(search_pos < end) {
        usb_descriptor_t *next = (usb_descriptor_t*)((uint8_t*)search_pos + search_pos->bLength);

        switch(search_pos->bDescriptorType) {
            case USB_INTERFACE_DESCRIPTOR: {
                usb_interface_descriptor_t *desc = (usb_interface_descriptor_t*)search_pos;
                if(interface_found) return search_pos;
                if(desc->bInterfaceClass != USB_HID_CLASS) break;
                if(desc->bInterfaceSubClass != HID_SUBCLASS_BOOT) break;
                interface_found = true;
                result->interface = desc->bInterfaceNumber;
                result->type = desc->bInterfaceProtocol;
                break;
            }

            case USB_ENDPOINT_DESCRIPTOR: {
                usb_endpoint_descriptor_t *desc = (usb_endpoint_descriptor_t*)search_pos;
                if(!interface_found) break;
                if(desc->bEndpointAddress & 0x80) {
                    /* IN endpoint */
                    result->in = usb_GetDeviceEndpoint(dev, desc->bEndpointAddress);
                } else {
                    /* OUT endpoint */
                    result->out = usb_GetDeviceEndpoint(dev, desc->bEndpointAddress);
                }
                break;
            }

            default:
                break;
        }
        search_pos = next;
    }

    return NULL;
}

static bool hid_IsKeyDownInReport(hid_keyboard_report_t *report, uint8_t key_code) {
    uint8_t i;
    for(i = 0; i < sizeof(report->pressed); i++) {
        if(report->pressed[i] == key_code)
            return true;
    }
    return false;
}

bool hid_IsKeyDown(hid_state_t *hid, uint8_t key_code) {
    uint8_t i;
    if(hid->type != HID_KEYBOARD) return false;

    return hid_IsKeyDownInReport(&hid->report.kb, key_code);
}

bool hid_IsModifierDown(hid_state_t *hid, uint8_t modifier) {
    if(hid->type != HID_KEYBOARD) return false;

    return hid->report.kb.modifiers & modifier;
}

usb_error_t hid_SetLEDs(hid_state_t *hid, uint8_t leds) {
    if(hid->type != HID_KEYBOARD) return USB_ERROR_NOT_SUPPORTED;
    if(hid->out) {
        return usb_Transfer(hid->out, &leds, 1, 10, NULL);
    } else {
        usb_control_setup_t setup = {0x21, 0x09, 0x200, 0, 1};
        setup.wIndex = hid->interface;
        return usb_DefaultControlTransfer(hid->dev, &setup, &leds, 1, NULL);
    }
}

bool his_IsMouseButtonDown(hid_state_t *hid, uint8_t button) {
    if(hid->type != HID_MOUSE) return false;

    return hid->report.mouse.buttons & (1 << button);
}

void hid_GetCursorDeltas(hid_state_t *hid, int24_t *x, int24_t *y){
    *x = hid->delta_x;
    *y = hid->delta_y;
    hid->delta_x = hid->delta_y = 0;
}

void hid_SetEventCallback(hid_state_t *hid, hid_callback_t callback, void *callback_data) {
    hid->callback = callback;
    hid->callback_data = callback_data;
}

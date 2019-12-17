#ifndef HID_HID_H
#define HID_HID_H

#include <stdbool.h>
#include <usbdrvce.h>

#define HID_SUBCLASS_BOOT 1

#define HID_KEYBOARD 1
#define HID_MOUSE 2

enum LEDs {
    LED_NUM_LOCK    = (1 << 0),
    LED_CAPS_LOCK   = (1 << 1),
    LED_SCROLL_LOCK = (1 << 2),
    LED_COMPOSE     = (1 << 3),
    LED_KANA        = (1 << 4)
};

typedef struct HID_KeyboardReport {
    uint8_t modifiers;
    uint8_t reserved_1;
    uint8_t pressed[6];
} hid_keyboard_report_t;

typedef struct HID_MouseReport {
    uint8_t buttons;
    int8_t x;
    int8_t y;
} hid_mouse_report_t;

typedef union {
    hid_keyboard_report_t kb;
    hid_mouse_report_t mouse;
} hid_report_t;

struct HID_State;


/**
 * Type of the function to be called when a HID event occurs
 * @param event Event type
 * @param code Key code, modifier code, or mouse button
 * @param callback_data Opaque pointer passed to \c hid_SetEventCallback
 */
typedef void (*hid_callback_t)(struct HID_State *hid, uint8_t event,
                               uint8_t code, void *callback_data);

typedef struct HID_State {
    bool active;
    bool stopped;
    uint8_t type;
    usb_device_t dev;
    usb_endpoint_t in;
    usb_endpoint_t out;
    uint8_t interface;
    uint8_t report_size;
    hid_report_t report;
    hid_report_t last_report;
    int24_t delta_x;
    int24_t delta_y;
    hid_callback_t callback;
    void *callback_data;
} hid_state_t;

/**
 * Search for HID boot interfaces
 * Should be called in a loop - \c search_pos should be initialized to
 * the configuration or interface descriptor, and set to the return value
 * of the previous loop's call on subsequent loops.
 * Sets result->interface to -1 if no HID interface was found
 * @note Set the configuration for \c dev before calling.
 * @param search_pos Pointer to the descriptor to search
 * @param end Pointer to the location in memory after the end of the descriptor
 * @param result The HID state for the found interface
 * @param dev The USB device the descriptor describes
 * @return The next value of \c search_pos
 */
usb_descriptor_t *hid_GetNext(usb_descriptor_t *search_pos, usb_descriptor_t *end,
                              hid_state_t *result, usb_device_t dev);

/**
 * Start listening to an HID interface.
 * @note Must be called before @param hid can be used with other functions.
 * @param hid HID state from \c hid_GetNext
 * @return USB_SUCCESS if initialization succeeded
 */
usb_error_t hid_Init(hid_state_t *hid);

/**
 * Stop listening on an HID interface
 * @note Call before freeing \c hid
 */
void hid_Stop(hid_state_t *hid);

/**
 * Set the idle time between updates for the HID interface.
 * @param time Time in 4 ms units
 * @return
 */
usb_error_t hid_SetIdle(hid_state_t *hid, uint8_t time);

/**
 * Check if a key is down
 * @param key_code Key to check
 * @return true if key is down, false otherwise
 */
bool hid_IsKeyDown(hid_state_t *hid, uint8_t key_code);

/**
 * Check if a modifier key (Ctrl, Alt, Shift, etc.) is down
 * @param modifier Modifier to check
 * @return true if modifier key is down, false otherwise
 */
bool hid_IsModifierDown(hid_state_t *hid, uint8_t modifier);

/**
 * Set the keyboard LEDs
 * @param leds New LED bitmap
 * @return USB_SUCCESS if LEDs set successfully
 */
usb_error_t hid_SetLEDs(hid_state_t *hid, uint8_t leds);

/**
 * Check if a mouse button is down
 * @param button Button to check
 * @return true if button is down, false otherwise
 */
bool his_IsMouseButtonDown(hid_state_t *hid, uint8_t button);

/**
 * Get the change in cursor position since the
 * last time this function was called
 * @param x Returns the change in x position
 * @param y Returns the change in y position
 */
void hid_GetCursorDeltas(hid_state_t *hid, int24_t *x, int24_t *y);

enum HID_Event {
    HID_EVENT_KEY_DOWN,
    HID_EVENT_KEY_UP,
    HID_EVENT_MODIFIER_DOWN,
    HID_EVENT_MODIFIER_UP,
    HID_EVENT_MOUSE_DOWN,
    HID_EVENT_MOUSE_UP,
    HID_EVENT_MOUSE_MOVE,

    HID_EVENT_DISCONNECTED
};

/**
 * Set the HID event handler function for an interface
 * @param callback Event handler function
 * @param callback_data Opaque pointer passed to event handler
 */
void hid_SetEventCallback(hid_state_t *hid, hid_callback_t callback, void *callback_data);

#endif //HID_HID_H

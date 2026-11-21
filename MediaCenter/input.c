// input.c: joystick and usb keyboard threads
// both turn presses into the same EV_x codes and put them in inputQ,
// so the menus and games never read the hardware directly
// jason su 501158090

#include "media.h"
#include "KBD.h"
#include "usbhost_kbd.h"

volatile uint32_t key_held  = 0;
volatile uint32_t kbd_state = 0;

// -------------------------------------------------------------------
// helpers used by the app threads
// -------------------------------------------------------------------
static void post (uint32_t ev) {
    osMessagePut (inputQ, ev, 0);       // drop the event if the queue is full
}

// empty appQ so an app doesn't start with old presses
void app_flush (void) {
    osEvent e;
    do {
        e = osMessageGet (appQ, 0);
    } while (e.status == osEventMessage);
}

// tell tMenu the app is finished
void app_done (void) {
    osMessagePut (inputQ, EV_DONE, osWaitForever);
}

// wait up to ms for the next event, EV_NONE on timeout
uint32_t app_get (uint32_t ms) {
    osEvent e = osMessageGet (appQ, ms);
    return (e.status == osEventMessage) ? e.value.v : EV_NONE;
}

// directions held right now on the joystick or the keyboard
uint32_t input_held (void) {
    uint32_t b = get_button ();
    uint32_t h = key_held;
    if (b & KBD_UP)    h |= HELD_UP;
    if (b & KBD_DOWN)  h |= HELD_DOWN;
    if (b & KBD_LEFT)  h |= HELD_LEFT;
    if (b & KBD_RIGHT) h |= HELD_RIGHT;
    return h;
}

// -------------------------------------------------------------------
// tJoystick: polls every 20 ms, only new presses become events
// (same edge detect with the previous value as lab 2)
// -------------------------------------------------------------------
void tJoystick (void const *argument) {
    uint32_t now, prev = 0, pressed;

    for (;;) {
        now = get_button ();
        pressed = now & ~prev;          // bits that just went down
        prev = now;

        if (pressed & KBD_UP)     post (EV_UP);
        if (pressed & KBD_DOWN)   post (EV_DOWN);
        if (pressed & KBD_LEFT)   post (EV_LEFT);
        if (pressed & KBD_RIGHT)  post (EV_RIGHT);
        if (pressed & KBD_SELECT) post (EV_SELECT);

        osDelay (20);
    }
}

#if USE_KEYBOARD
// -------------------------------------------------------------------
// keyboard usage codes (hid usage table, keyboard page)
// -------------------------------------------------------------------
#define K_A      0x04
#define K_D      0x07
#define K_P      0x13
#define K_S      0x16
#define K_W      0x1A
#define K_ENTER  0x28
#define K_ESC    0x29
#define K_SPACE  0x2C
#define K_RIGHT  0x4F
#define K_LEFT   0x50
#define K_DOWN   0x51
#define K_UP     0x52

static uint32_t key_to_event (uint8_t k) {
    switch (k) {
        case K_UP:    case K_W: return EV_UP;
        case K_DOWN:  case K_S: return EV_DOWN;
        case K_LEFT:  case K_A: return EV_LEFT;
        case K_RIGHT: case K_D: return EV_RIGHT;
        case K_ENTER: case K_SPACE: case K_P: return EV_SELECT;
        case K_ESC:   return EV_ESC;
        default:      return EV_NONE;
    }
}

static uint32_t event_to_held (uint32_t ev) {
    switch (ev) {
        case EV_UP:    return HELD_UP;
        case EV_DOWN:  return HELD_DOWN;
        case EV_LEFT:  return HELD_LEFT;
        case EV_RIGHT: return HELD_RIGHT;
        default:       return 0;
    }
}

// a boot keyboard report: byte 0 modifiers, byte 1 reserved, bytes 2..7 keys
static void handle_report (const uint8_t *rep, uint8_t *last) {
    uint32_t i, j, ev, held = 0, isnew;

    for (i = 2; i < 8; i++) {
        if (rep[i] < 4) continue;                   // 0 = none, 1..3 = errors
        ev = key_to_event (rep[i]);
        held |= event_to_held (ev);

        isnew = 1;                                  // was it already down last time?
        for (j = 2; j < 8; j++) {
            if (last[j] == rep[i]) isnew = 0;
        }
        if (isnew && ev != EV_NONE) post (ev);
    }
    key_held = held;
    for (i = 0; i < 8; i++) last[i] = rep[i];
}
#endif

// -------------------------------------------------------------------
// tKeyboard: only runs while the game center is open
// SIG_HOST_ON starts host mode, SIG_HOST_OFF stops it and answers SIG_ACK
// -------------------------------------------------------------------
void tKeyboard (void const *argument) {
#if USE_KEYBOARD
    osEvent e;
    uint8_t rep[8], last[8];
    uint32_t i;
#endif

    for (;;) {
        osSignalWait (SIG_HOST_ON, osWaitForever);

#if USE_KEYBOARD
        kbd_state = 0;
        for (i = 0; i < 8; i++) last[i] = 0;
        usbh_start ();

        for (;;) {
            e = osSignalWait (SIG_HOST_OFF, 10);    // poll every 10 ms
            if (e.status == osEventSignal) break;

            switch (usbh_poll (rep)) {
                case USBH_REPORT:  kbd_state = 1; handle_report (rep, last); break;
                case USBH_READY:   kbd_state = 1; break;
                case USBH_NO_DEV:  kbd_state = 0; key_held = 0; break;
                case USBH_ERROR:   kbd_state = 2; key_held = 0; break;
            }
        }

        usbh_stop ();
        kbd_state = 0;
        key_held  = 0;
#else
        osSignalWait (SIG_HOST_OFF, osWaitForever);
#endif
        osSignalSet (tid_tMenu, SIG_ACK);
    }
}

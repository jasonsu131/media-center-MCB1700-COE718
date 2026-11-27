// input.c: joystick and keyboard threads
// the keyboard is either the pc's keyboard through the debugger (USE_KEYBOARD 2)
// or a usb keyboard on the board (USE_KEYBOARD 1). both inputs turn presses
// into the same EV_x codes and put them in inputQ,
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

#if USE_KEYBOARD == 2
// -------------------------------------------------------------------
// pc keyboard through the debugger
// uVision writes each key typed in the Debug (printf) Viewer into
// ITM_RxBuffer, ITM_CheckChar / ITM_ReceiveChar (core_cm3.h) read it.
// tJoystick calls pc_kbd_poll() every 20 ms.
// arrow keys may arrive as ESC [ A..D, so ESC itself is not used (Q = back).
// a typed character has no "key up", so a direction counts as held for
// HOLD_MS after its last character (the pc's key repeat keeps it held)
// -------------------------------------------------------------------
volatile int32_t ITM_RxBuffer = ITM_RXBUFFER_EMPTY;

#define HOLD_MS     160

static uint32_t hold_left[4];           // ms of hold left for up, down, left, right
static uint32_t esc_state;              // 0 normal, 1 got ESC, 2 got ESC [

static uint32_t char_to_event (int32_t c) {
    switch (c) {
        case 'w': case 'W': case '8': return EV_UP;
        case 's': case 'S': case '2': return EV_DOWN;
        case 'a': case 'A': case '4': return EV_LEFT;
        case 'd': case 'D': case '6': return EV_RIGHT;
        case '\r': case '\n': case ' ': case 'p': case 'P': case '5': return EV_SELECT;
        case 'q': case 'Q': return EV_ESC;
        default: return EV_NONE;
    }
}

static void pc_key (uint32_t ev) {
    if (ev == EV_NONE) return;
    kbd_state = 1;                      // a key got through, so it works
    post (ev);
    if (ev >= EV_UP && ev <= EV_RIGHT) hold_left[ev - EV_UP] = HOLD_MS;
}

static void pc_kbd_poll (uint32_t ms) {
    int32_t c;
    uint32_t i, bit = 0;

    while (ITM_CheckChar ()) {
        c = ITM_ReceiveChar ();
        if (esc_state == 1) {                       // after ESC
            esc_state = 0;
            if (c == '[' || c == 'O') { esc_state = 2; continue; }
        } else if (esc_state == 2) {                // ESC [ x = arrow key
            esc_state = 0;
            if (c == 'A') pc_key (EV_UP);
            if (c == 'B') pc_key (EV_DOWN);
            if (c == 'C') pc_key (EV_RIGHT);
            if (c == 'D') pc_key (EV_LEFT);
            continue;
        }
        if (c == 27) { esc_state = 1; continue; }
        pc_key (char_to_event (c));
    }

    for (i = 0; i < 4; i++) {                       // update the held mask
        if (hold_left[i]) {
            hold_left[i] = (hold_left[i] > ms) ? hold_left[i] - ms : 0;
            bit |= 1u << i;                         // HELD_UP..HELD_RIGHT are bits 0..3
        }
    }
    key_held = bit;
}
#endif

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
#if USE_KEYBOARD == 2
        pc_kbd_poll (20);               // keys typed on the pc
#endif

        osDelay (20);
    }
}

#if USE_KEYBOARD == 1
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
#if USE_KEYBOARD == 1
    osEvent e;
    uint8_t rep[8], last[8];
    uint32_t i;
#endif

    for (;;) {
        osSignalWait (SIG_HOST_ON, osWaitForever);

#if USE_KEYBOARD == 1
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

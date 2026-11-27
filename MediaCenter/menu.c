// menu.c: tMenu, the main menu and game menu state machine
// tMenu is the only reader of inputQ. while an app runs it forwards
// events to appQ, and the app sends EV_DONE back when it is finished
// jason su 501158090

#include "media.h"
#include "GLCD.h"
#include "LED.h"

volatile uint32_t game_sel = 0;

// -------------------------------------------------------------------
// menu states
// -------------------------------------------------------------------
#define ST_MAIN     0
#define ST_GAMES    1
#define ST_APP      2       // gallery, mp3 or a game is running

static const char *main_items[3] = { "Photo Gallery", "MP3 Player", "Game Center" };
static const char *game_items[3] = { "Snake", "Pong", "Back" };

// -------------------------------------------------------------------
// drawing
// -------------------------------------------------------------------
// one item row: arrow and yellow highlight when selected (like lab 3)
static void draw_item (unsigned int row, const char *name, unsigned int sel) {
    char line[21];
    unsigned int i;

    line[0] = sel ? '>' : ' ';
    line[1] = ' ';
    for (i = 2; i < 20; i++) line[i] = (*name) ? *name++ : ' ';
    line[20] = 0;

    if (sel) ui_text (row, 0, 1, Blue,  Yellow, line);
    else     ui_text (row, 0, 1, White, Black,  line);
}

static void draw_list (const char **items, unsigned int sel) {
    unsigned int i;
    for (i = 0; i < 3; i++) draw_item (3 + 2 * i, items[i], i == sel);
}

static void draw_kbd_status (void) {
#if USE_KEYBOARD == 2
    ui_text (27, 1, 0, (kbd_state == 1) ? Green : LightGrey, Black,
             (kbd_state == 1) ? "Keyboard: PC keyboard ready            " : "Keyboard: type in uVision printf window");
#elif USE_KEYBOARD == 1
    static const char *msg[3] = { "Keyboard: not connected", "Keyboard: ready        ",
                                  "Keyboard: not supported" };
    ui_text (27, 1, 0, (kbd_state == 1) ? Green : LightGrey, Black, msg[kbd_state > 2 ? 0 : kbd_state]);
#else
    ui_text (27, 1, 0, LightGrey, Black, "Keyboard: off (joystick only)");
#endif
}

static void draw_main (unsigned int sel) {
    LCD_LOCK ();
    ui_header ("Media Center");
    draw_list (main_items, sel);
    ui_text (28, 1, 0, LightGrey, Black, "UP/DOWN to move, SELECT to open");
    ui_text (29, 1, 0, LightGrey, Black, "COE 718  Jason Su 501158090");
    LCD_UNLOCK ();
    LED_Out (1u << sel);
}

static void draw_games (unsigned int sel) {
    LCD_LOCK ();
    ui_header ("Game Center");
    draw_list (game_items, sel);
    draw_kbd_status ();
#if USE_KEYBOARD == 2
    ui_text (28, 1, 0, LightGrey, Black, "Keys: WASD move, Enter/P pause, Q quit");
#else
    ui_text (28, 1, 0, LightGrey, Black, "Keys: arrows/WASD, Enter pause, Esc quit");
#endif
    ui_text (29, 1, 0, LightGrey, Black, "Joystick: SELECT pause, DOWN when paused quits");
    LCD_UNLOCK ();
    LED_Out (0x20u << sel);                 // leds 5..7 in the game menu
}

static void move_sel (unsigned int *sel, const char **items, uint32_t ev) {
    unsigned int old = *sel;
    if (ev == EV_UP)   *sel = (*sel + 2) % 3;  // wraps around
    if (ev == EV_DOWN) *sel = (*sel + 1) % 3;
    if (*sel != old) {
        LCD_LOCK ();
        draw_item (3 + 2 * old,  items[old],  0);
        draw_item (3 + 2 * *sel, items[*sel], 1);
        LCD_UNLOCK ();
    }
}

// -------------------------------------------------------------------
// usb host mode on and off (the keyboard thread does the work)
// -------------------------------------------------------------------
static void host_on (void) {
    osSignalSet (tid_tKeyboard, SIG_HOST_ON);
}

static void host_off (void) {
    osSignalSet (tid_tKeyboard, SIG_HOST_OFF);
    osSignalWait (SIG_ACK, 2000);           // wait until the controller is free
}

// -------------------------------------------------------------------
// tMenu
// -------------------------------------------------------------------
void tMenu (void const *argument) {
    unsigned int state = ST_MAIN, back = ST_MAIN;
    unsigned int msel = 0, gsel = 0, last_kbd = 99;
    osEvent e;
    uint32_t ev;

    draw_main (msel);

    for (;;) {
        // in the game menu wake up twice a second to refresh the keyboard status
        e  = osMessageGet (inputQ, (state == ST_GAMES) ? 500 : osWaitForever);
        ev = (e.status == osEventMessage) ? e.value.v : EV_NONE;

        switch (state) {

        case ST_MAIN:
            move_sel (&msel, main_items, ev);
            LED_Out (1u << msel);
            if (ev == EV_SELECT) {
                if (msel == 2) {                        // game center
                    state = ST_GAMES;
                    gsel = 0;
                    last_kbd = 99;
                    host_on ();
                    draw_games (gsel);
                } else {                                // gallery or mp3
                    state = ST_APP;
                    back  = ST_MAIN;
                    app_flush ();
                    osSignalSet (msel == 0 ? tid_tGallery : tid_tAudio, SIG_START);
                }
            }
            break;

        case ST_GAMES:
            if (kbd_state != last_kbd) {                // keyboard seen / plugged in or out
                last_kbd = kbd_state;
                LCD_LOCK ();
                draw_kbd_status ();
                LCD_UNLOCK ();
            }
            move_sel (&gsel, game_items, ev);
            if (ev == EV_ESC || (ev == EV_SELECT && gsel == 2)) {
                host_off ();                            // free the usb controller
                state = ST_MAIN;
                draw_main (msel);
            } else if (ev == EV_SELECT) {
                state = ST_APP;
                back  = ST_GAMES;
                game_sel = gsel;
                app_flush ();
                osSignalSet (tid_tGame, SIG_START);
            }
            break;

        case ST_APP:
            if (ev == EV_DONE) {                        // app finished
                state = back;
                if (state == ST_MAIN) draw_main (msel);
                else { last_kbd = 99; draw_games (gsel); }
            } else if (ev != EV_NONE) {
                osMessagePut (appQ, ev, 0);             // pass it on to the app
            }
            break;
        }
    }
}

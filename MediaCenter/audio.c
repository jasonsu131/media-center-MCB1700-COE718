// audio.c: tAudio, the mp3 player screen
// USE_AUDIO 1: usb speaker from the USBAudio example (usbdmain.c),
//              the pc streams the music and the interrupts play it.
//              the board connects when the player opens and
//              disconnects when it closes
// USE_AUDIO 2: backup, plays a song stored in flash (song.c)
// jason su 501158090

#include <stdio.h>
#include "media.h"
#include "GLCD.h"
#include "LED.h"

#if USE_AUDIO == 1
// in usbdmain.c (the USBAudio example, its main() turned into these two)
extern void audio_hw_start (void);
extern void audio_hw_stop  (void);
extern uint32_t audio_usb_state (void);  // 0 waiting for pc, 1 ready, 2 playing
extern uint16_t PotVal;             // 0..255, read by the timer interrupt
#endif

#if USE_AUDIO == 2
// in song.c
void        song_init       (void);
void        song_stop       (void);
void        song_play_pause (void);
void        song_restart    (void);
uint32_t    song_playing    (void);
uint32_t    song_pos_s      (void);
uint32_t    song_len_s      (void);
uint32_t    song_read_pot   (void);
const char *song_name       (void);
#endif

// -------------------------------------------------------------------
// start and stop
// -------------------------------------------------------------------
static void audio_start (void) {
#if USE_AUDIO == 1
    // timer0 must start from a clean state (prescaler 0) or the
    // sample rate would be wrong after the title screen
    LPC_TIM0->TCR = 2;
    LPC_TIM0->PR  = 0;
    LPC_TIM0->TCR = 0;
    audio_hw_start ();                      // dac, adc, timer0, USB_Init, USB_Connect(TRUE)
#elif USE_AUDIO == 2
    song_init ();                           // dac, adc, timer0 (paused)
#endif
}

static void audio_stop (void) {
#if USE_AUDIO == 1
    audio_hw_stop ();                       // USB_Connect(FALSE), timer off, dac silent
#elif USE_AUDIO == 2
    song_stop ();
#endif
}

// -------------------------------------------------------------------
// volume from the potentiometer, 0..100 for the bar
// -------------------------------------------------------------------
static unsigned int pot_percent (void) {
#if USE_AUDIO == 1
    return ((unsigned int)PotVal * 100) / 255;
#elif USE_AUDIO == 2
    return song_read_pot ();
#else
    return 0;
#endif
}

// -------------------------------------------------------------------
// splash screen: a simple speaker drawing and sound waves
// -------------------------------------------------------------------
static void draw_splash (void) {
    LCD_LOCK ();
    ui_header ("MP3 Player");

    ui_rect (110, 50, 24, 40, LightGrey);           // speaker body
    ui_rect (134, 40, 8, 60, LightGrey);
    ui_rect (142, 30, 8, 80, LightGrey);
    ui_rect (162, 58, 4, 24, Cyan);                 // waves
    ui_rect (174, 48, 4, 44, Cyan);
    ui_rect (186, 38, 4, 64, Cyan);

#if USE_AUDIO == 1
    ui_center (5, 1, Yellow, Black, "Connecting...");
    ui_center (19, 0, LightGrey, Black, "Play an MP3 or YouTube video on the PC");
    ui_text (28, 1, 0, LightGrey, Black, "Turn the pot for volume");
    ui_text (29, 1, 0, LightGrey, Black, "SELECT or DOWN: back (usb disconnects)");
#elif USE_AUDIO == 2
    ui_center (19, 0, LightGrey, Black, song_name ());
    ui_text (28, 1, 0, LightGrey, Black, "SELECT play/pause  LEFT restart");
    ui_text (29, 1, 0, LightGrey, Black, "DOWN back   pot = volume");
#else
    ui_center (5, 1, Red, Black, "Audio not added");
    ui_center (19, 0, LightGrey, Black, "Set USE_AUDIO in media.h");
#endif
    ui_text (22, 2, 0, White, Black, "Volume");
    ui_rect (59, 175, 202, 14, White);              // bar frame
    LCD_UNLOCK ();
}

static void draw_volume (unsigned int pct) {
    LCD_LOCK ();
    ui_rect (60, 176, 2 * pct, 12, Green);
    ui_rect (60 + 2 * pct, 176, 200 - 2 * pct, 12, Black);
    LCD_UNLOCK ();
    LED_Out ((1u << ((pct * 8) / 101 + 1)) - 1);    // leds as a vu style bar
}

#if USE_AUDIO == 1
// status line: what the pc is doing with the board
static void draw_usb_state (uint32_t st) {
    static const char *msg[3] = { "Waiting for PC", "USB speaker ready", "Playing from PC" };
    static const unsigned short col[3] = { Yellow, Cyan, Green };

    if (st > 2) st = 0;
    LCD_LOCK ();
    ui_rect (0, 120, 320, 24, Black);               // clear row 5 (font 1)
    ui_center (5, 1, col[st], Black, msg[st]);
    LCD_UNLOCK ();
}
#endif

#if USE_AUDIO == 2
// "Playing  0:12 / 0:30" line and a thin progress bar
static void draw_status (void) {
    char line[24];
    uint32_t p = song_pos_s (), t = song_len_s ();

    sprintf (line, "%s %u:%02u / %u:%02u", song_playing () ? "Playing" : "Paused ",
             p / 60, p % 60, t / 60, t % 60);
    LCD_LOCK ();
    ui_center (5, 1, song_playing () ? Green : Yellow, Black, line);
    ui_rect (60, 200, t ? (200 * p) / t : 0, 4, Cyan);
    ui_rect (60 + (t ? (200 * p) / t : 0), 200, 200 - (t ? (200 * p) / t : 0), 4, DarkGrey);
    LCD_UNLOCK ();
}
#endif

// -------------------------------------------------------------------
// tAudio
// -------------------------------------------------------------------
void tAudio (void const *argument) {
    unsigned int pct, last;
    uint32_t ev;
#if USE_AUDIO == 1
    uint32_t st, last_st;
#endif

    for (;;) {
        osSignalWait (SIG_START, osWaitForever);

        draw_splash ();
        audio_start ();
        last = 999;
#if USE_AUDIO == 1
        last_st = 99;
#endif

        for (;;) {
            ev = app_get (50);                      // refresh every 50 ms
#if USE_AUDIO == 2
            if (ev == EV_DOWN || ev == EV_ESC) break;
            if (ev == EV_SELECT) song_play_pause ();
            if (ev == EV_LEFT)   song_restart ();
            draw_status ();
#elif USE_AUDIO == 1
            if (ev == EV_SELECT || ev == EV_DOWN || ev == EV_ESC) break;
            st = audio_usb_state ();
            if (st != last_st) {
                draw_usb_state (st);
                last_st = st;
            }
#else
            if (ev == EV_SELECT || ev == EV_ESC) break;
#endif
            pct = pot_percent ();
            if (pct != last) {
                draw_volume (pct);
                last = pct;
            }
        }

        audio_stop ();                              // timer off, usb disconnected
        LED_Out (0);
        app_done ();
    }
}

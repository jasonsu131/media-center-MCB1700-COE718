// game.c: tGame runs snake or pong, plus helpers both games share
// jason su 501158090

#include "media.h"
#include "GLCD.h"
#include "LED.h"
#include "game.h"
#include <stdio.h>

// -------------------------------------------------------------------
// small random number generator (seeded from the rtos tick)
// -------------------------------------------------------------------
static uint32_t seed = 1;

void game_seed (void) {
    seed ^= osKernelSysTick ();
    if (seed == 0) seed = 1;
}

uint32_t game_rand (uint32_t n) {
    seed = seed * 1103515245u + 12345u;
    return ((seed >> 16) & 0x7FFF) % n;
}

// -------------------------------------------------------------------
// score bar across the top (rows 0 and 1 of the 6x8 font = 16 px)
// -------------------------------------------------------------------
void game_bar (const char *left, const char *right) {
    char line[54];
    unsigned int i, n = 0;

    for (i = 0; i < 53; i++) line[i] = ' ';
    line[53] = 0;
    while (left[n] && n < 26) { line[1 + n] = left[n]; n++; }
    n = 0;
    while (right[n]) n++;
    if (n > 26) n = 26;
    for (i = 0; i < n; i++) line[52 - n + i] = right[i];

    LCD_LOCK ();
    ui_text (0, 0, 0, Yellow, Navy, line);
    ui_rect (0, 8, 320, 8, Navy);
    LCD_UNLOCK ();
}

// -------------------------------------------------------------------
// pause: SELECT resumes, DOWN or ESC quits (works with only the joystick)
// returns 1 if the player wants to quit
// -------------------------------------------------------------------
int game_pause (void) {
    uint32_t ev;

    game_bar ("PAUSED", "SELECT resume  DOWN quit");
    LED_Out (0xFF);
    for (;;) {
        ev = app_get (osWaitForever);
        if (ev == EV_SELECT) { LED_Out (0); return 0; }
        if (ev == EV_DOWN || ev == EV_ESC) { LED_Out (0); return 1; }
    }
}

// -------------------------------------------------------------------
// game over box, waits for SELECT / ESC (or 10 s) before going back
// -------------------------------------------------------------------
void game_over (const char *line1, const char *line2) {
    uint32_t ev, i;

    LCD_LOCK ();
    ui_rect (60, 84, 200, 80, White);
    ui_rect (62, 86, 196, 76, Maroon);
    ui_center (4, 1, White, Maroon, line1);
    ui_center (17, 0, Yellow, Maroon, line2);
    ui_center (19, 0, LightGrey, Maroon, "SELECT to continue");
    LCD_UNLOCK ();

    app_flush ();                               // ignore keys pressed while dying
    for (i = 0; i < 20; i++) {                  // flash the leds while waiting
        LED_Out ((i & 1) ? 0x0F : 0xF0);
        ev = app_get (500);
        if (ev == EV_SELECT || ev == EV_ESC) break;
    }
    LED_Out (0);
}

// -------------------------------------------------------------------
// tGame
// -------------------------------------------------------------------
void tGame (void const *argument) {
    for (;;) {
        osSignalWait (SIG_START, osWaitForever);
        game_seed ();

        if (game_sel == 0) snake_run ();
        else               pong_run ();

        app_done ();
    }
}

// pong.c: player (left) against the board (right), first to 5 points
// the paddle uses the "held" state so it moves smoothly while a key is down
// jason su 501158090

#include "media.h"
#include "GLCD.h"
#include "LED.h"
#include "game.h"
#include <stdio.h>

#define TOP         16              // score bar height
#define FIELD_H     (240 - TOP)
#define PW          6               // paddle width
#define PH          40              // paddle height
#define PL_X        8               // player paddle x
#define CPU_X       (320 - 8 - PW)  // cpu paddle x
#define BS          6               // ball size
#define FRAME_MS    25
#define PL_SPEED    5               // px per frame
#define CPU_SPEED   3               // slower than the player so it can be beaten
#define WIN_SCORE   5
#define FP          16              // ball position is fixed point x16

#define C_BG        Black
#define C_PADDLE    White
#define C_BALL      Yellow
#define C_NET       DarkGrey

static int pl_y, cpu_y;             // paddle tops
static int bx, by, vx, vy;          // ball (fixed point)
static unsigned int pl_score, cpu_score;

// -------------------------------------------------------------------
// drawing
// -------------------------------------------------------------------
static void rect (int x, int y, int w, int h, unsigned short c) {
    LCD_LOCK ();
    ui_rect (x, y, w, h, c);
    LCD_UNLOCK ();
}

static void draw_net (void) {
    int y;
    for (y = TOP + 4; y < 240; y += 16) rect (159, y, 2, 8, C_NET);
}

// move a paddle: erase the part it left, draw the part it entered
static void move_paddle (int x, int *py, int ny) {
    if (ny < TOP) ny = TOP;
    if (ny > 240 - PH) ny = 240 - PH;
    if (ny == *py) return;
    if (ny > *py) {
        rect (x, *py, PW, ny - *py, C_BG);
        rect (x, *py + PH, PW, ny - *py, C_PADDLE);
    } else {
        rect (x, ny + PH, PW, *py - ny, C_BG);
        rect (x, ny, PW, *py - ny, C_PADDLE);
    }
    *py = ny;
}

static void show_score (void) {
    char l[27];
    sprintf (l, "PONG   You %u : %u CPU", pl_score, cpu_score);
    game_bar (l, "First to 5");
    LED_Out (((1u << pl_score) - 1) | (((1u << cpu_score) - 1) << (8 - cpu_score)));
}

// put the ball in the middle and serve it towards dir (-1 left, 1 right)
static void serve (int dir) {
    bx = (160 - BS / 2) * FP;
    by = (TOP + FIELD_H / 2 - BS / 2) * FP;
    vx = dir * 3 * FP;
    vy = ((int)game_rand (5) - 2) * FP / 2;
}

// -------------------------------------------------------------------
// one game of pong
// -------------------------------------------------------------------
void pong_run (void) {
    uint32_t ev, held;
    int obx, oby, x, y, target, hit, quit = 0;
    char msg[30];

    LCD_LOCK ();
    GLCD_Clear (C_BG);
    LCD_UNLOCK ();

    pl_score = cpu_score = 0;
    pl_y = cpu_y = TOP + (FIELD_H - PH) / 2;
    rect (PL_X,  pl_y,  PW, PH, C_PADDLE);
    rect (CPU_X, cpu_y, PW, PH, C_PADDLE);
    draw_net ();
    show_score ();
    serve (game_rand (2) ? 1 : -1);
    osDelay (500);

    while (!quit && pl_score < WIN_SCORE && cpu_score < WIN_SCORE) {
        // keys: only pause and quit come as events, movement uses the held state
        while ((ev = app_get (0)) != EV_NONE) {
            if (ev == EV_ESC) { quit = 1; break; }
            if (ev == EV_SELECT) {
                if (game_pause ()) { quit = 1; break; }
                show_score ();
            }
        }
        if (quit) break;

        // player paddle
        held = input_held ();
        if (held & HELD_UP)   move_paddle (PL_X, &pl_y, pl_y - PL_SPEED);
        if (held & HELD_DOWN) move_paddle (PL_X, &pl_y, pl_y + PL_SPEED);

        // cpu paddle follows the ball when it is coming, otherwise goes back to the middle
        target = (vx > 0) ? (by / FP + BS / 2 - PH / 2) : (TOP + (FIELD_H - PH) / 2);
        if (target > cpu_y + CPU_SPEED)      move_paddle (CPU_X, &cpu_y, cpu_y + CPU_SPEED);
        else if (target < cpu_y - CPU_SPEED) move_paddle (CPU_X, &cpu_y, cpu_y - CPU_SPEED);

        // ball
        obx = bx / FP;  oby = by / FP;
        bx += vx;  by += vy;
        y = by / FP;
        if (y < TOP)       { by = TOP * FP;             vy = -vy; }
        if (y > 240 - BS)  { by = (240 - BS) * FP;      vy = -vy; }
        x = bx / FP;  y = by / FP;

        // paddle hits: bounce, speed up a little, angle from where it hit
        hit = 0;
        if (vx < 0 && x <= PL_X + PW && x + BS >= PL_X && y + BS >= pl_y && y <= pl_y + PH) {
            hit = 1;  bx = (PL_X + PW) * FP;
            vy = ((y + BS / 2) - (pl_y + PH / 2)) * FP / 5;
        }
        if (vx > 0 && x + BS >= CPU_X && x <= CPU_X + PW && y + BS >= cpu_y && y <= cpu_y + PH) {
            hit = 1;  bx = (CPU_X - BS) * FP;
            vy = ((y + BS / 2) - (cpu_y + PH / 2)) * FP / 5;
        }
        if (hit) {
            vx = -vx;
            if (vx > 0 && vx < 7 * FP)  vx += FP / 2;
            if (vx < 0 && vx > -7 * FP) vx -= FP / 2;
        }
        x = bx / FP;  y = by / FP;

        // erase the old ball
        rect (obx, oby, BS, BS, C_BG);
        if (obx > 150 && obx < 166) draw_net ();     // ball crossed the net

        if (x >= 0 && x <= 320 - BS) {
            rect (x, y, BS, BS, C_BALL);              // still in play
        } else {
            // a point (the ball is off the screen, so it isn't drawn)
            if (x < 0) cpu_score++; else pl_score++;
            rect (PL_X,  pl_y,  PW, PH, C_PADDLE);  // ball may have clipped a paddle
            rect (CPU_X, cpu_y, PW, PH, C_PADDLE);
            show_score ();
            serve (x < 0 ? -1 : 1);                   // serve to whoever lost the point
            osDelay (600);
        }

        osDelay (FRAME_MS);
    }

    if (!quit) {
        sprintf (msg, "You %u : %u CPU", pl_score, cpu_score);
        game_over (pl_score >= WIN_SCORE ? "YOU WIN" : "CPU WINS", msg);
    }
    LED_Out (0);
}

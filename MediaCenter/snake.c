// snake.c: snake on a 20 x 14 grid of 16 px cells
// only the new head, the old head and the old tail are redrawn each tick,
// because clearing the whole lcd over spi is far too slow for a game
// jason su 501158090

#include "media.h"
#include "GLCD.h"
#include "LED.h"
#include "game.h"
#include <stdio.h>

#define COLS        20
#define ROWS        14
#define CELL        16
#define TOP         16              // score bar height
#define MAXLEN      (COLS * ROWS)

#define TICK_START  150             // ms per move at the start
#define TICK_MIN    70              // fastest speed
#define TICK_STEP   5               // faster by this much per food

#define C_BG        Black
#define C_BODY      Green
#define C_HEAD      Yellow
#define C_FOOD      Red

// body as a circular buffer of cells, head at [head], tail len-1 behind it
static uint8_t  bx[MAXLEN], by[MAXLEN];
static uint8_t  occ[ROWS][COLS];    // 1 where the body is, for fast collision checks
static uint16_t head, len;
static uint8_t  fx, fy;             // food
static uint32_t best = 0;

static const int8_t dx[4] = { 0, 0, -1, 1 };   // up, down, left, right
static const int8_t dy[4] = { -1, 1, 0, 0 };

// -------------------------------------------------------------------
// drawing
// -------------------------------------------------------------------
static void cell (unsigned int x, unsigned int y, unsigned short c) {
    LCD_LOCK ();
    ui_rect (x * CELL + 1, TOP + y * CELL + 1, CELL - 2, CELL - 2, c);
    LCD_UNLOCK ();
}

static void show_score (void) {
    char l[27], r[27];
    sprintf (l, "SNAKE   Score: %u", (unsigned int)(len - 3));
    sprintf (r, "Best: %u", (unsigned int)best);
    game_bar (l, r);
    LED_Out ((len - 3) & 0xFF);
}

static void place_food (void) {
    unsigned int tries = 0;
    do {
        fx = game_rand (COLS);
        fy = game_rand (ROWS);
    } while (occ[fy][fx] && ++tries < 500);
    cell (fx, fy, C_FOOD);
}

// -------------------------------------------------------------------
// map an event to a direction index, -1 if it isn't a direction
// -------------------------------------------------------------------
static int ev_dir (uint32_t ev) {
    switch (ev) {
        case EV_UP:    return 0;
        case EV_DOWN:  return 1;
        case EV_LEFT:  return 2;
        case EV_RIGHT: return 3;
        default:       return -1;
    }
}

// -------------------------------------------------------------------
// one game of snake
// -------------------------------------------------------------------
void snake_run (void) {
    unsigned int i, x, y, tail, tick = TICK_START;
    int dir = 3, next = 3, d, nx, ny, quit = 0, dead = 0;
    uint32_t ev;
    char msg[30];

    // clear the field
    LCD_LOCK ();
    GLCD_Clear (C_BG);
    LCD_UNLOCK ();
    for (y = 0; y < ROWS; y++)
        for (x = 0; x < COLS; x++) occ[y][x] = 0;

    // start with 3 cells in the middle, moving right
    len = 3;
    head = 2;
    for (i = 0; i < 3; i++) {
        bx[i] = 4 + i;  by[i] = ROWS / 2;
        occ[by[i]][bx[i]] = 1;
        cell (bx[i], by[i], (i == 2) ? C_HEAD : C_BODY);
    }
    show_score ();
    place_food ();

    while (!quit && !dead) {
        // read every event that came in since the last tick
        while ((ev = app_get (0)) != EV_NONE) {
            if (ev == EV_ESC) { quit = 1; break; }
            if (ev == EV_SELECT) {
                if (game_pause ()) { quit = 1; break; }
                show_score ();
                continue;
            }
            d = ev_dir (ev);
            if (d >= 0 && (d >> 1) != (dir >> 1)) next = d;   // no turning back on itself
        }
        if (quit) break;
        dir = next;

        nx = bx[head] + dx[dir];
        ny = by[head] + dy[dir];
        tail = (head + MAXLEN - (len - 1)) % MAXLEN;

        // walls
        if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) { dead = 1; break; }

        if (nx == fx && ny == fy) {
            // eat: grow by one, tail stays
            len++;
            if (len - 3 > best) best = len - 3;
            if (tick > TICK_MIN) tick -= TICK_STEP;
        } else {
            // normal move: tail cell is freed first, so following the tail is allowed
            occ[by[tail]][bx[tail]] = 0;
            cell (bx[tail], by[tail], C_BG);
        }

        // own body
        if (occ[ny][nx]) { dead = 1; break; }

        cell (bx[head], by[head], C_BODY);          // old head becomes body
        head = (head + 1) % MAXLEN;
        bx[head] = nx;  by[head] = ny;
        occ[ny][nx] = 1;
        cell (nx, ny, C_HEAD);

        if (nx == fx && ny == fy) {
            show_score ();
            if (len >= MAXLEN) break;               // filled the screen
            place_food ();
        }

        osDelay (tick);
    }

    if (!quit) {
        sprintf (msg, "Score %u   Best %u", (unsigned int)(len - 3), (unsigned int)best);
        game_over (len >= MAXLEN ? "YOU WIN" : "GAME OVER", msg);
    }
    LED_Out (0);
}

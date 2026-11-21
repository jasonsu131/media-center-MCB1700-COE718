// game.h: helpers shared by snake.c and pong.c
// jason su 501158090

#ifndef __GAME_H
#define __GAME_H

#include <stdint.h>

void     game_seed  (void);
uint32_t game_rand  (uint32_t n);                       // 0 .. n-1
void     game_bar   (const char *left, const char *right);
int      game_pause (void);                             // 1 = quit
void     game_over  (const char *line1, const char *line2);

#endif

// ui.c: small lcd drawing helpers shared by every screen
// every function here expects the caller to hold the lcd mutex
// jason su 501158090

#include "media.h"
#include "GLCD.h"

// filled rectangle (bargraph at full value fills the whole window)
void ui_rect (unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned short c) {
    if (w == 0 || h == 0) return;
    GLCD_SetTextColor (c);
    GLCD_Bargraph (x, y, w, h, 1024);
}

// text at a row and column (font 1 = 16x24, 20 columns; font 0 = 6x8, 53 columns)
void ui_text (unsigned int row, unsigned int col, unsigned char font,
              unsigned short fg, unsigned short bg, const char *s) {
    GLCD_SetBackColor (bg);
    GLCD_SetTextColor (fg);
    GLCD_DisplayString (row, col, font, (unsigned char *)s);
}

// text centred on the screen
void ui_center (unsigned int row, unsigned char font,
                unsigned short fg, unsigned short bg, const char *s) {
    unsigned int len = 0, cols = font ? 20 : 53;
    while (s[len]) len++;
    ui_text (row, (len < cols) ? (cols - len) / 2 : 0, font, fg, bg, s);
}

// blue title bar across the top, same look on every screen
void ui_header (const char *title) {
    GLCD_Clear (Black);
    ui_rect (0, 0, 320, 24, Blue);
    ui_center (0, 1, Yellow, Blue, title);
}

// gallery.c: tGallery, carousel photo gallery
// LEFT / RIGHT = previous / next picture, SELECT = back to the menu
// jason su 501158090

#include "media.h"
#include "GLCD.h"
#include "LED.h"
#include <stdio.h>

// -------------------------------------------------------------------
// pictures (rgb565 arrays made with GIMP or img2c.py)
// each header defines one array, so they are only included here
// -------------------------------------------------------------------
#include "pic_sunset.h"
#include "pic_tmu.h"
#include "pic_board.h"

typedef struct {
    const unsigned short *data;
    unsigned int w, h;
    const char *name;
} picture_t;

static const picture_t pics[] = {
    { pic_sunset, PIC_SUNSET_W, PIC_SUNSET_H, "Sunset" },
    { pic_tmu,    PIC_TMU_W,    PIC_TMU_H,    "TMU colours" },
    { pic_board,  PIC_BOARD_W,  PIC_BOARD_H,  "Circuit" },
};

#define NUM_PICS  (sizeof (pics) / sizeof (pics[0]))

// -------------------------------------------------------------------
// draw one picture centred under the title bar
// -------------------------------------------------------------------
static void show (unsigned int i) {
    char line[54];
    const picture_t *p = &pics[i];
    unsigned int x = (320 - p->w) / 2;
    unsigned int y = 24 + (200 - p->h) / 2;     // area between header and footer

    LCD_LOCK ();
    ui_rect (0, 24, 320, 200, Black);           // clear the old picture
    GLCD_Bitmap (x, y, p->w, p->h, (unsigned char *)p->data);
    ui_rect (0, 224, 320, 8, Black);            // only the caption row
    sprintf (line, "<  %u/%u  %s  >", i + 1, (unsigned int)NUM_PICS, p->name);
    ui_center (28, 0, White, Black, line);
    LCD_UNLOCK ();

    LED_Out (1u << (i % 8));                    // led shows the picture number
}

// -------------------------------------------------------------------
// tGallery
// -------------------------------------------------------------------
void tGallery (void const *argument) {
    unsigned int cur;
    uint32_t ev;

    for (;;) {
        osSignalWait (SIG_START, osWaitForever);

        LCD_LOCK ();
        ui_header ("Photo Gallery");
        ui_text (29, 1, 0, LightGrey, Black, "LEFT/RIGHT: flip   SELECT: back");
        LCD_UNLOCK ();

        cur = 0;
        show (cur);

        for (;;) {
            ev = app_get (osWaitForever);
            if (ev == EV_SELECT || ev == EV_ESC) break;
            if (ev == EV_RIGHT) { cur = (cur + 1) % NUM_PICS;            show (cur); }
            if (ev == EV_LEFT)  { cur = (cur + NUM_PICS - 1) % NUM_PICS; show (cur); }
        }

        LED_Out (0);
        app_done ();
    }
}

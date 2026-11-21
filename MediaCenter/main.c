// coe718 final project: media center
// main.c: hardware init, 3 second title screen, rtos objects, threads
// jason su 501158090

#include "media.h"
#include "GLCD.h"
#include "LED.h"
#include "KBD.h"

// -------------------------------------------------------------------
// title screen text (20 characters max each)
// -------------------------------------------------------------------
#define TITLE_1     "MEDIA CENTER"
#define TITLE_2     "Jason Su"

// colours (rgb565) so this doesn't depend on GLCD.h names
#define TS_BLACK    0x0000
#define TS_WHITE    0xFFFF
#define TS_RED      0xF800
#define TS_YELLOW   0xFFE0
#define TS_GREEN    0x07E0
#define TS_CYAN     0x07FF
#define TS_MAGENTA  0xF81F
#define TS_NAVY     0x000F
#define TS_GREY     0x7BEF
#define TS_DARK     0x4208

// -------------------------------------------------------------------
// rtos objects
// -------------------------------------------------------------------
osMessageQDef (inputQ, 16, uint32_t);
osMessageQDef (appQ, 16, uint32_t);
osMutexDef (lcd_mutex);

osMessageQId inputQ;
osMessageQId appQ;
osMutexId    lcd_id;

// stack sizes are in bytes, they come out of OS_PRIVSTKSIZE in RTX_Conf_CM.c
osThreadDef (tMenu,     osPriorityNormal,      1, 1024);
osThreadDef (tJoystick, osPriorityAboveNormal, 1, 512);
osThreadDef (tKeyboard, osPriorityAboveNormal, 1, 1024);
osThreadDef (tGallery,  osPriorityNormal,      1, 512);
osThreadDef (tAudio,    osPriorityNormal,      1, 1024);
osThreadDef (tGame,     osPriorityNormal,      1, 1024);

osThreadId tid_tMenu, tid_tJoystick, tid_tKeyboard;
osThreadId tid_tGallery, tid_tAudio, tid_tGame;

// -------------------------------------------------------------------
// timer0 counts microseconds, used for the exact 3 second timing
// (the rtos isn't running yet so osDelay can't be used here)
// -------------------------------------------------------------------
static void timer_start (void) {
    LPC_SC->PCONP    |= (1u << 1);              // power timer0
    LPC_SC->PCLKSEL0 &= ~(3u << 2);             // pclk = cclk/4
    LPC_TIM0->TCR = 2;                          // reset
    LPC_TIM0->PR  = (SystemCoreClock / 4000000u) - 1;   // 1 tick = 1 us
    LPC_TIM0->TCR = 1;                          // go
}

static void wait_until_ms (unsigned int ms) {
    while (LPC_TIM0->TC < ms * 1000u);
}

// -------------------------------------------------------------------
// title screen drawing helpers
// -------------------------------------------------------------------
static void rect (unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned short c) {
    GLCD_SetTextColor (c);
    GLCD_Bargraph (x, y, w, h, 1024);
}

static void center_text (unsigned int row, unsigned char font, const char *s) {
    unsigned int len = 0, cols = font ? 20 : 53, col;
    while (s[len]) len++;
    col = (len < cols) ? (cols - len) / 2 : 0;
    GLCD_DisplayString (row, col, font, (unsigned char *)s);
}

// all 20 chip pins in one colour
static void draw_pins (unsigned short c) {
    unsigned int k;
    for (k = 0; k < 5; k++) {
        rect (88,  100 + 14 * k, 12, 6, c);     // left
        rect (220, 100 + 14 * k, 12, 6, c);     // right
        rect (116 + 20 * k, 82,  6, 10, c);     // top
        rect (116 + 20 * k, 172, 6, 10, c);     // bottom
    }
}

// -------------------------------------------------------------------
// the title screen from lab 3b, takes 3000 ms total
// -------------------------------------------------------------------
static void title_screen (void) {
    static const unsigned short coreCol[5] = { TS_RED, TS_YELLOW, TS_GREEN, TS_CYAN, TS_MAGENTA };
    unsigned int i, k;

    timer_start ();
    GLCD_Clear (TS_BLACK);

    // double border
    rect (4, 4, 312, 3, TS_CYAN);   rect (4, 233, 312, 3, TS_CYAN);
    rect (4, 4, 3, 232, TS_CYAN);   rect (313, 4, 3, 232, TS_CYAN);
    rect (10, 10, 300, 1, TS_MAGENTA);  rect (10, 229, 300, 1, TS_MAGENTA);
    rect (10, 10, 1, 220, TS_MAGENTA);  rect (309, 10, 1, 220, TS_MAGENTA);

    // title text
    GLCD_SetBackColor (TS_BLACK);
    GLCD_SetTextColor (TS_YELLOW);
    center_text (1, 1, TITLE_1);
    GLCD_SetTextColor (TS_WHITE);
    center_text (2, 1, TITLE_2);

    // traces going out from the side pins
    for (k = 0; k < 5; k++) {
        rect (70,  102 + 14 * k, 18, 2, TS_DARK);
        rect (232, 102 + 14 * k, 18, 2, TS_DARK);
    }

    // chip body, die, pin 1 marker
    rect (100, 92, 120, 80, TS_GREY);
    rect (112, 102, 96, 60, TS_NAVY);
    rect (104, 96, 6, 6, TS_YELLOW);
    draw_pins (TS_GREY);

    // five core squares (all off at the start)
    for (k = 0; k < 5; k++) {
        rect (120 + 18 * k, 116, 14, 14, TS_DARK);
    }

    // progress bar frame
    rect (40, 190, 240, 14, TS_WHITE);
    rect (41, 191, 238, 12, TS_BLACK);

    // 20 steps x 150 ms = 3000 ms
    for (i = 0; i < 20; i++) {
        draw_pins ((i & 1) ? TS_GREY : TS_YELLOW);          // pins pulse
        if ((i % 4) == 3) {                                 // a core lights every 4 steps
            k = i / 4;
            rect (120 + 18 * k, 116, 14, 14, coreCol[k]);
        }
        rect (42, 192, (236 * (i + 1)) / 20, 10, TS_CYAN);  // progress bar
        LED_Out (1u << (i % 8));                            // led chase
        wait_until_ms ((i + 1) * 150);                      // exact timing
    }

    LED_Out (0);

    // give timer0 back in a clean state, the audio code sets it up again
    LPC_TIM0->TCR = 2;
    LPC_TIM0->PR  = 0;
    LPC_TIM0->TCR = 0;
}

// -------------------------------------------------------------------
// main
// -------------------------------------------------------------------
int main (void) {
    GLCD_Init ();
    LED_Init ();
    KBD_Init ();
    title_screen ();                        // 3 second title, then the menu

    osKernelInitialize ();                  // initialize CMSIS-RTOS

    inputQ = osMessageCreate (osMessageQ (inputQ), NULL);
    appQ   = osMessageCreate (osMessageQ (appQ), NULL);
    lcd_id = osMutexCreate (osMutex (lcd_mutex));

    tid_tGallery  = osThreadCreate (osThread (tGallery),  NULL);
    tid_tAudio    = osThreadCreate (osThread (tAudio),    NULL);
    tid_tGame     = osThreadCreate (osThread (tGame),     NULL);
    tid_tKeyboard = osThreadCreate (osThread (tKeyboard), NULL);
    tid_tJoystick = osThreadCreate (osThread (tJoystick), NULL);
    tid_tMenu     = osThreadCreate (osThread (tMenu),     NULL);

    osKernelStart ();                       // start thread execution
    osDelay (osWaitForever);
    return 0;
}

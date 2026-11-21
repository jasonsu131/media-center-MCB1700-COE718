// song.c: plays a song stored in flash (USE_AUDIO 2)
// timer0 interrupts at the song's sample rate and writes the next
// sample to the dac (P0.26). no usb is needed. the pot sets the volume
// jason su 501158090

#include "media.h"

#if USE_AUDIO == 2

#include "song.h"           // made by tools/wav2c.py: song_data[], SONG_LEN, SONG_RATE

// -------------------------------------------------------------------
// player state (shared with tAudio in audio.c)
// -------------------------------------------------------------------
static volatile uint32_t pos;       // next sample to play
static volatile uint32_t playing;   // 1 = playing, 0 = paused
static volatile uint32_t vol;       // 0..256, set from the pot

// -------------------------------------------------------------------
// timer0: one sample per interrupt
// -------------------------------------------------------------------
void TIMER0_IRQHandler (void) {
    int32_t s;

    LPC_TIM0->IR = 1;                               // clear mr0 flag

    if (!playing) return;

    s = (int32_t)song_data[pos] - 128;              // -128..127
    s = 512 + ((s * 4 * (int32_t)vol) >> 8);        // scale to 10 bit, apply volume
    if (s < 0)    s = 0;
    if (s > 1023) s = 1023;
    LPC_DAC->DACR = (uint32_t)s << 6;

    if (++pos >= SONG_LEN) {                        // end of song: stop and rewind
        pos = 0;
        playing = 0;
        LPC_DAC->DACR = 512u << 6;
    }
}

// -------------------------------------------------------------------
// setup and control (called from tAudio)
// -------------------------------------------------------------------
void song_init (void) {
    uint32_t pclk;

    LPC_PINCON->PINSEL1 &= ~((3u << 18) | (3u << 20));
    LPC_PINCON->PINSEL1 |=  ((1u << 18) | (2u << 20)); // P0.25 = AD0.2 (pot), P0.26 = AOUT
    LPC_SC->PCONP |= (1u << 12);                    // adc power on
    LPC_ADC->ADCR = 0x00200E04;                     // channel 2, clk div 15, on
    LPC_DAC->DACR = 512u << 6;                      // middle = silence

    switch ((LPC_SC->PCLKSEL0 >> 2) & 3u) {         // timer0 peripheral clock
        case 1:  pclk = SystemCoreClock;     break;
        case 2:  pclk = SystemCoreClock / 2; break;
        case 3:  pclk = SystemCoreClock / 8; break;
        default: pclk = SystemCoreClock / 4; break;
    }

    playing = 0;
    pos = 0;
    vol = 128;

    LPC_TIM0->TCR = 2;                              // reset
    LPC_TIM0->PR  = 0;
    LPC_TIM0->MR0 = pclk / SONG_RATE - 1;
    LPC_TIM0->MCR = 3;                              // interrupt and reset on mr0
    LPC_TIM0->TCR = 1;                              // go
    NVIC_EnableIRQ (TIMER0_IRQn);
}

void song_stop (void) {
    playing = 0;
    NVIC_DisableIRQ (TIMER0_IRQn);
    LPC_TIM0->TCR = 0;
    LPC_DAC->DACR = 512u << 6;
}

void song_play_pause (void) { playing = !playing; }
void song_restart    (void) { pos = 0; playing = 1; }
uint32_t song_playing (void) { return playing; }

// seconds played and total length, for the progress bar
uint32_t song_pos_s (void) { return pos / SONG_RATE; }
uint32_t song_len_s (void) { return SONG_LEN / SONG_RATE; }
const char *song_name (void) { return SONG_NAME; }

// -------------------------------------------------------------------
// pot -> volume. returns 0..100 for the bar
// -------------------------------------------------------------------
uint32_t song_read_pot (void) {
    uint32_t v, n = 0;

    LPC_ADC->ADCR |= (1u << 24);                    // start conversion
    do {
        v = LPC_ADC->ADGDR;
    } while (!(v & (1u << 31)) && ++n < 10000);     // done bit (with a timeout)
    v = (v >> 4) & 0xFFF;                           // 12 bit result

#if POT_INVERT
    v = 0xFFF - v;                                  // counter clockwise = louder
#endif
    vol = (v * 256) / 0xFFF;
    return (v * 100) / 0xFFF;
}

#endif

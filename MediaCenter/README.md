# Media Center: setup guide

Keil uVision project for the COE 718 Media Center on the MCB1700. It was built from the Lab 3b Q1 project, so the device, pack versions, debugger and flash settings are the same as your labs.

**Status:** built in Keil and tested on the MCB1700. The menu, photo gallery, MP3 player (song stored in flash) and both games work with the joystick. The USB keyboard driver (`usbhost_kbd.c`) is included but did not work on the board, so the default build uses the joystick only (`USE_KEYBOARD 0`).

## 1. First build (no audio yet)

1. Copy the whole `MediaCenter` folder somewhere local (or to your U: drive) and open `MediaCenter.uvprojx`.
2. If uVision asks for packs, use the same ones as the labs: LPC1700_DFP 2.6.0, CMSIS 5.7.0 (Keil RTX 4.82).
3. Build (F7).
4. Flash (F8) and press RESET. You should see the 3 s title screen, then the main menu.
5. Test the menu, the gallery and both games with the joystick before adding audio.

## 2. Config settings

These are already set in the project. Check them if something behaves oddly.

**Options for Target > Device:** LPC1768.

**Manage Run-Time Environment:** CMSIS > CORE, CMSIS > RTOS (API) > Keil RTX, Device > Startup.

**Options for Target > Target:**
- IROM1 0x00000000, size 0x80000 (ticked)
- IRAM1 0x10000000, size 0x8000 (ticked)
- IRAM2 0x2007C000, size 0x8000 **not ticked** (untick the box in the "default" column). The USB host driver keeps its buffers in AHB SRAM at 0x20083000, so the linker must not put RTX data there.
- Use MicroLIB: ticked (needed for `sprintf`)

**Options for Target > Debug / Utilities:** same as your labs (ULINK, LPC17xx IAP 512 kB flash algorithm).

**RTX_Conf_CM.c** (under CMSIS in the project window, open it with the Configuration Wizard tab):

| Setting | Value | Why |
| --- | --- | --- |
| Number of concurrent running threads (OS_TASKCNT) | 8 | 6 threads plus main and idle |
| Default thread stack size (OS_STKSIZE) | 128 words | |
| Main thread stack size (OS_MAINSTKSIZE) | 128 words | |
| Number of threads with user-provided stack (OS_PRIVCNT) | 6 | every thread sets its own stack in `osThreadDef` |
| Total stack size for those threads (OS_PRIVSTKSIZE) | 1280 words | 1024 + 512 + 1024 + 512 + 1024 + 1024 bytes = 5120 bytes |
| Stack overflow checking (OS_STKCHECK) | on | catches the Lab 3 type stack problem |
| RTOS kernel timer input frequency (OS_CLOCK) | 100000000 | the chip runs at 100 MHz. At 10000000 every delay runs 10x fast (Lab 4) |
| RTX timer tick interval (OS_TICK) | 1000 us | so `osDelay(20)` is 20 ms |
| Round-robin (OS_ROBIN) | off | priorities decide who runs |

**Switches in media.h:**

| Switch | Default | Meaning |
| --- | --- | --- |
| `USE_AUDIO` | 2 | 2 = song stored in flash, 1 = USB speaker (USBAudio example), 0 = screen only |
| `USE_KEYBOARD` | 0 | set to 0 for joystick-only games if the keyboard driver gives trouble |
| `POT_INVERT` | 1 | flips the pot reading so counter clockwise = louder |

## 3. MP3 player

### Song in flash (USE_AUDIO 2, the default)
`song.c` plays `song.h` through the DAC (P0.26, the board speaker). Timer0 interrupts 8000 times a second and writes one sample. No USB is used. The pot sets the volume.

Controls: SELECT play/pause, LEFT restart, DOWN back to the menu.

The project comes with a 13 s sample (Ode to Joy, generated). To use your own song:
1. In Audacity, open the song, select up to about 25 s, and use File > Export > Export Selected Audio as WAV.
2. Run `python tools/wav2c.py mysong.wav 25 "Song name"` in the project folder. It writes `song.h` (8 bit, mono, 8000 Hz).
3. Rebuild. 25 s is about 200 KB of flash, which fits next to the three pictures.

### USB speaker (USE_AUDIO 1)

The USBAudio example files are in the project (group **USB Audio**), adapted for this project:
- `usbdmain.c`: the example's `main()` is now `audio_hw_start()`, plus a new `audio_hw_stop()` that calls `USB_Connect(FALSE)`. `SystemFrequency` became `SystemCoreClock`.
- `usbhw.c`, `usbdmain.c`: register names changed to the ones in the Keil pack's `LPC17xx.h` (for example `DevIntSt` became `USBDevIntSt`, `LPC_ADC->CR` became `LPC_ADC->ADCR`). The example's own older `LPC17xx.h` is **not** used.
- `usbcore.c`, `usbuser.c`, `usbdesc.c`, `adcuser.c` and the headers are unchanged.

The example uses USB DMA, so its buffers must be in AHB SRAM at 0x20080000. That needs its scatter file:
- Options for Target > **Linker**: untick "Use Memory Layout from Target Dialog", Scatter File = `.\DMA.sct`.

Volume direction: the spec says counter clockwise = louder. If it goes the wrong way, change `POT_INVERT` in `media.h`.

## 4. USB keyboard

- Plug the keyboard into the board's USB host (type A) connector. Check the MCB1700 user's guide for the jumper settings that route the USB lines to the host connector and turn on its 5 V supply.
- Host mode only runs while the Game Center menu is open. Leaving it with **Back** shuts the host down before the MP3 player can use the USB controller.
- The bottom of the Game Center screen shows "Keyboard: ready" when the keyboard enumerated. "not supported" means a device was found but setup failed.
- Hubs are not supported. Plug the keyboard in directly.

## 5. Controls

The keyboard only works inside the Game Center, because the USB controller is in host mode only there.

| Where | Joystick | Keyboard |
| --- | --- | --- |
| Main menu | UP / DOWN move, SELECT opens | not used |
| Gallery | LEFT / RIGHT flip, SELECT back | not used |
| MP3 player | SELECT play/pause, LEFT restart, DOWN back, pot = volume | not used |
| Game menu | UP / DOWN move, SELECT opens | arrows or W/S, Enter, Esc = back |
| Snake | directions | arrows or WASD |
| Pong | UP / DOWN (hold) | arrows or W/S (hold) |
| In a game | SELECT pause, then DOWN quits | Enter/P pause, Esc quit |

## 6. Adding your own pictures

GIMP route (from the project handout): flip vertically, scale to about 200 x 150, export as C source in RGB565, change `guint8` to `unsigned char`.

Faster route: `python tools/img2c.py photo.jpg pic_cat` makes `pic_cat.h` (scaled, flipped, RGB565). Then:
1. add `#include "pic_cat.h"` at the top of `gallery.c`
2. add a row to `pics[]`: `{ pic_cat, PIC_CAT_W, PIC_CAT_H, "My cat" },`

Each 200 x 150 picture is 60,000 bytes of flash. Keep it to about 6 pictures.

## 7. Files

| File | What it does |
| --- | --- |
| `main.c` | hardware init, Lab 3b title screen, creates queues, mutex and threads |
| `media.h` | build switches, event codes, signals, shared declarations |
| `menu.c` | `tMenu`: main menu and game menu state machine, forwards events to the running app |
| `input.c` | `tJoystick` (20 ms poll) and `tKeyboard` (USB host reports), both feed `inputQ` |
| `ui.c` | rectangle, text and header helpers |
| `gallery.c` | `tGallery`: carousel of pictures |
| `audio.c` | `tAudio`: MP3 splash screen, play/pause, progress and volume bars |
| `song.c`, `song.h` | song player (timer0 interrupt to the DAC) and the song samples |
| `tools/wav2c.py` | WAV to `song.h` converter |
| `game.c`, `game.h` | `tGame`, random numbers, score bar, pause and game over screens |
| `snake.c`, `pong.c` | the two games |
| `usbhost_kbd.c`, `.h` | small OHCI host driver for one boot protocol keyboard |
| `pic_*.h` | three sample pictures (replace with your own) |
| `GLCD_SPI_LPC1700.c`, `LED.c`, `KBD.c` and headers | drivers from the labs, unchanged |
| `tools/img2c.py` | picture to RGB565 header converter |

## 8. Threads

| Thread | Priority | Stack | Job |
| --- | --- | --- | --- |
| tJoystick | AboveNormal | 512 B | reads the joystick every 20 ms |
| tKeyboard | AboveNormal | 1024 B | USB host mode, keyboard reports every 10 ms |
| tMenu | Normal | 1024 B | menus, only reader of `inputQ` |
| tGallery | Normal | 512 B | photo gallery |
| tAudio | Normal | 1024 B | MP3 player screen |
| tGame | Normal | 1024 B | Snake or Pong |

If a thread freezes after a few screens, raise its stack size in `main.c` and raise `OS_PRIVSTKSIZE` by the same amount (bytes / 4).

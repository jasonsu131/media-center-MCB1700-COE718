// usbhost_kbd.c: minimal usb host driver for one keyboard on the lpc1768
//
// the lpc1768 host controller follows the ohci standard. this driver is
// the smallest part of it a keyboard needs:
//   - port reset and enumeration with control transfers
//   - SET_PROTOCOL(boot) so every keyboard sends the same 8 byte report
//   - one interrupt endpoint polled for reports
// there are no usb interrupts here (the USBAudio example owns
// USB_IRQHandler), everything is polled from tKeyboard. hubs and
// multiple devices are not supported, plug the keyboard straight in.
//
// all ohci structures must be in ahb sram because the host controller
// reads them with dma. they sit in the last 4 kB of ahb sram bank 1
// (0x20083000), away from 0x2007C000 where the linker puts rtx data.
// jason su 501158090

#include "media.h"
#include "usbhost_kbd.h"

// -------------------------------------------------------------------
// ohci structures
// -------------------------------------------------------------------
typedef struct {                    // endpoint descriptor, 16 byte aligned
    volatile uint32_t Control;
    volatile uint32_t TailTd;
    volatile uint32_t HeadTd;
    volatile uint32_t Next;
} HCED;

typedef struct {                    // general transfer descriptor, 16 byte aligned
    volatile uint32_t Control;
    volatile uint32_t CurrBufPtr;
    volatile uint32_t Next;
    volatile uint32_t BufEnd;
} HCTD;

typedef struct {                    // host controller communication area, 256 byte aligned
    volatile uint32_t IntTable[32];
    volatile uint32_t FrameNumber;
    volatile uint32_t DoneHead;
    volatile uint8_t  Reserved[120];
} HCCA;

// fixed places at the top of ahb sram bank 1
#define USB_RAM         0x20083000u
#define hcca            ((HCCA *)(USB_RAM + 0x000))
#define td              ((HCTD *)(USB_RAM + 0x100))     // 6 tds
#define ed_ctrl         ((HCED *)(USB_RAM + 0x180))
#define ed_int          ((HCED *)(USB_RAM + 0x190))
#define setup_buf       ((volatile uint8_t *)(USB_RAM + 0x200))
#define data_buf        ((volatile uint8_t *)(USB_RAM + 0x210))  // 256 bytes
#define rep_buf         ((volatile uint8_t *)(USB_RAM + 0x310))  // 8 bytes
#define USB_RAM_USED    0x320

// -------------------------------------------------------------------
// register bits (ohci spec / lpc17xx user manual chapter 12)
// -------------------------------------------------------------------
#define CTRL_PLE        (1u << 2)       // periodic list enable
#define CTRL_CLE        (1u << 4)       // control list enable
#define CTRL_OPER       (2u << 6)       // operational state

#define CMD_HCR         (1u << 0)       // host controller reset
#define CMD_CLF         (1u << 1)       // control list filled

#define RH_LPS          (1u << 0)       // clear global power
#define RH_LPSC         (1u << 16)      // set global power

#define PORT_CCS        (1u << 0)       // device connected
#define PORT_PRS        (1u << 4)       // write: start port reset
#define PORT_PPS        (1u << 8)       // write: port power on
#define PORT_LSDA       (1u << 9)       // read: low speed device / write: port power off
#define PORT_CSC        (1u << 16)      // connect status changed
#define PORT_PRSC       (1u << 20)      // port reset finished

#define TD_ROUND        (1u << 18)      // short packets are ok
#define TD_SETUP        (0u << 19)
#define TD_OUT          (1u << 19)
#define TD_IN           (2u << 19)
#define TD_NOINT        (7u << 21)      // no done queue interrupt
#define TD_DATA0        (2u << 24)
#define TD_DATA1        (3u << 24)
#define TD_TOGGLE_ED    (0u << 24)      // toggle carried by the ed
#define TD_CC_NEW       (0xFu << 28)    // not accessed yet

#define ED_DIR_IN       (2u << 11)
#define ED_LOWSPEED     (1u << 13)
#define ED_SKIP         (1u << 14)

// -------------------------------------------------------------------
// driver state
// -------------------------------------------------------------------
#define S_NONE          0
#define S_READY         1
#define S_ERROR         2

static uint32_t state = S_NONE;
static uint32_t lowspeed, mps0;

// root hub port the keyboard is on. the mcb1700 type A host connector is
// wired to usb port 2 (P0.31 D+2, D-2), port 1 (P0.29, P0.30) is the device
// connector used by the mp3 player. both ports are run as host here and the
// driver uses whichever one a keyboard shows up on
static volatile uint32_t *portsc = 0;
#define PORTSC          (*portsc)
static HCTD *int_cur, *int_dummy;

// -------------------------------------------------------------------
// helpers
// -------------------------------------------------------------------
static void td_clear (HCTD *t) {
    t->Control = 0;  t->CurrBufPtr = 0;  t->Next = 0;  t->BufEnd = 0;
}

// wait until the ed has no tds left, 0 ok, -1 halted (stall / error), -2 timeout
static int wait_ed (HCED *ed, uint32_t ms) {
    while (ms--) {
        if ((ed->HeadTd & ~0xFu) == (ed->TailTd & ~0xFu)) return 0;
        if (ed->HeadTd & 1u) return -1;
        osDelay (1);
    }
    return -2;
}

// -------------------------------------------------------------------
// control transfer on ed_ctrl: setup, optional data stage, status
// returns bytes received for IN requests, or -1 on error
// -------------------------------------------------------------------
static int ctrl_xfer (uint8_t type, uint8_t req, uint16_t value, uint16_t index,
                      uint16_t length, uint8_t *data) {
    uint32_t in = type & 0x80;
    uint32_t i, n = 0;
    HCTD *setup = &td[0], *dstage = &td[1], *status = &td[2], *dummy = &td[3];
    int r;

    setup_buf[0] = type;           setup_buf[1] = req;
    setup_buf[2] = value & 0xFF;   setup_buf[3] = value >> 8;
    setup_buf[4] = index & 0xFF;   setup_buf[5] = index >> 8;
    setup_buf[6] = length & 0xFF;  setup_buf[7] = length >> 8;
    if (!in) for (i = 0; i < length; i++) data_buf[i] = data[i];

    // setup stage, always DATA0
    setup->Control    = TD_SETUP | TD_NOINT | TD_DATA0 | TD_CC_NEW;
    setup->CurrBufPtr = (uint32_t)setup_buf;
    setup->BufEnd     = (uint32_t)setup_buf + 7;

    if (length) {
        // data stage, starts with DATA1
        setup->Next        = (uint32_t)dstage;
        dstage->Control    = (in ? TD_IN : TD_OUT) | TD_ROUND | TD_NOINT | TD_DATA1 | TD_CC_NEW;
        dstage->CurrBufPtr = (uint32_t)data_buf;
        dstage->BufEnd     = (uint32_t)data_buf + length - 1;
        dstage->Next       = (uint32_t)status;
    } else {
        setup->Next = (uint32_t)status;
    }

    // status stage, opposite direction, zero length, DATA1
    status->Control    = ((in && length) ? TD_OUT : TD_IN) | TD_NOINT | TD_DATA1 | TD_CC_NEW;
    status->CurrBufPtr = 0;
    status->BufEnd     = 0;
    status->Next       = (uint32_t)dummy;
    td_clear (dummy);

    // hand the chain to the controller
    ed_ctrl->TailTd = (uint32_t)dummy;
    ed_ctrl->HeadTd = (uint32_t)setup;              // also clears halt and toggle carry
    LPC_USB->HcCommandStatus = CMD_CLF;

    r = wait_ed (ed_ctrl, 500);
    if (r != 0) {
        // take the leftover tds off the ed so the next transfer starts clean
        ed_ctrl->Control |= ED_SKIP;
        osDelay (2);
        ed_ctrl->HeadTd = (uint32_t)dummy;
        ed_ctrl->Control &= ~ED_SKIP;
        return -1;
    }

    // condition code 0 = no error on every stage
    if ((setup->Control >> 28) != 0 || (status->Control >> 28) != 0) return -1;
    if (length) {
        if ((dstage->Control >> 28) != 0) return -1;
        // CurrBufPtr is 0 when the whole buffer was filled, else it points after the last byte
        n = dstage->CurrBufPtr ? (dstage->CurrBufPtr - (uint32_t)data_buf) : length;
        if (in) for (i = 0; i < n; i++) data[i] = data_buf[i];
    }
    return (int)n;
}

// -------------------------------------------------------------------
// interrupt endpoint: two tds take turns being the real one and the dummy
// -------------------------------------------------------------------
static void int_queue (void) {
    HCTD *t  = int_dummy;           // old dummy becomes the new transfer
    HCTD *nd = int_cur;             // finished td becomes the new dummy

    td_clear (nd);
    t->Control    = TD_IN | TD_ROUND | TD_NOINT | TD_TOGGLE_ED | TD_CC_NEW;
    t->CurrBufPtr = (uint32_t)rep_buf;
    t->BufEnd     = (uint32_t)rep_buf + 7;
    t->Next       = (uint32_t)nd;

    int_cur   = t;
    int_dummy = nd;
    ed_int->TailTd = (uint32_t)nd;  // the controller now sees one td to run
}

static void int_stop (void) {
    uint32_t i;
    LPC_USB->HcControl &= ~CTRL_PLE;
    for (i = 0; i < 32; i++) hcca->IntTable[i] = 0;
}

// -------------------------------------------------------------------
// port reset, wait for the controller to say it's done
// -------------------------------------------------------------------
static void port_reset (void) {
    uint32_t i;
    PORTSC = PORT_PRS;
    for (i = 0; i < 100 && !(PORTSC & PORT_PRSC); i++) osDelay (1);
    PORTSC = PORT_PRSC;                             // write 1 to clear
    osDelay (20);                                   // recovery time
}

// -------------------------------------------------------------------
// enumerate the keyboard, 0 = ok
// -------------------------------------------------------------------
static int enumerate (void) {
    uint8_t buf[255];
    uint32_t total, cfg, i, blen, iface = 0, cur_iface = 0, is_kbd = 0;
    uint32_t ep = 0, ep_mps = 8;

    lowspeed = (PORTSC & PORT_LSDA) ? ED_LOWSPEED : 0;

    // address 0, endpoint 0, max packet 8 until we know better
    ed_ctrl->Control = lowspeed | (8u << 16);

    // first 8 bytes of the device descriptor give the ep0 max packet size
    if (ctrl_xfer (0x80, 6, 0x0100, 0, 8, buf) < 8) return -1;
    mps0 = buf[7];
    if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64) mps0 = 8;
    ed_ctrl->Control = lowspeed | (mps0 << 16);

    // SET_ADDRESS 1
    if (ctrl_xfer (0x00, 5, 1, 0, 0, 0) < 0) return -1;
    osDelay (10);
    ed_ctrl->Control = 1u | lowspeed | (mps0 << 16);

    // configuration descriptor header, then the whole thing
    if (ctrl_xfer (0x80, 6, 0x0200, 0, 9, buf) < 9) return -1;
    total = buf[2] | (buf[3] << 8);
    cfg   = buf[5];
    if (total > sizeof (buf)) total = sizeof (buf);
    if (ctrl_xfer (0x80, 6, 0x0200, 0, total, buf) < 9) return -1;

    // find a boot keyboard interface (class 3, subclass 1, protocol 1)
    // and its interrupt in endpoint
    for (i = 0; i + 1 < total; i += blen) {
        blen = buf[i];
        if (blen < 2) break;
        if (buf[i + 1] == 4 && i + 7 < total) {                 // interface
            cur_iface = buf[i + 2];
            is_kbd = (buf[i + 5] == 3 && buf[i + 6] == 1 && buf[i + 7] == 1);
        }
        if (buf[i + 1] == 5 && is_kbd && ep == 0 && i + 4 < total) {    // endpoint
            if ((buf[i + 2] & 0x80) && (buf[i + 3] & 3) == 3) {
                ep     = buf[i + 2] & 0x0F;
                ep_mps = buf[i + 4];
                iface  = cur_iface;
            }
        }
    }
    if (ep == 0) return -1;                                     // not a keyboard

    if (ctrl_xfer (0x00, 9, cfg, 0, 0, 0) < 0) return -1;       // SET_CONFIGURATION
    if (ctrl_xfer (0x21, 0x0B, 0, iface, 0, 0) < 0) return -1;  // SET_PROTOCOL boot
    ctrl_xfer (0x21, 0x0A, 0, iface, 0, 0);                     // SET_IDLE 0 (some keyboards stall, that's ok)

    // interrupt in endpoint, polled every frame through all 32 hcca slots
    ed_int->Control = 1u | (ep << 7) | ED_DIR_IN | lowspeed | (ep_mps << 16);
    ed_int->Next    = 0;
    int_cur   = &td[4];
    int_dummy = &td[5];
    td_clear (int_cur);
    td_clear (int_dummy);
    ed_int->TailTd = (uint32_t)int_dummy;
    ed_int->HeadTd = (uint32_t)int_dummy;                       // empty, toggle DATA0
    for (i = 0; i < 32; i++) hcca->IntTable[i] = (uint32_t)ed_int;
    int_queue ();
    LPC_USB->HcControl |= CTRL_PLE;
    return 0;
}

// -------------------------------------------------------------------
// public functions
// -------------------------------------------------------------------
void usbh_start (void) {
    uint32_t i;

    LPC_SC->PCONP |= (1u << 31);                    // power the usb block
    LPC_USB->OTGClkCtrl = 0x19;                     // host, otg and ahb clocks
    for (i = 0; i < 100000 && (LPC_USB->OTGClkSt & 0x19) != 0x19; i++);
    LPC_USB->OTGStCtrl = 0x1;                       // PORT_FUNC 01: port 1 and port 2 both host

    // pins: P0.29 D+1, P0.30 D-1, P0.31 D+2 (D-2 has its own pin),
    // P1.19 USB_PPWR, P1.22 USB_PWRD, P1.27 USB_OVRCR
    LPC_PINCON->PINSEL1 &= ~((3u << 26) | (3u << 28) | (3u << 30));
    LPC_PINCON->PINSEL1 |=  ((1u << 26) | (1u << 28) | (1u << 30));
    LPC_PINCON->PINSEL3 &= ~((3u << 6) | (3u << 12) | (3u << 22));
    LPC_PINCON->PINSEL3 |=  ((2u << 6) | (2u << 12) | (2u << 22));

    // clear our part of ahb sram
    for (i = 0; i < USB_RAM_USED; i += 4) *(volatile uint32_t *)(USB_RAM + i) = 0;

    // reset the host controller
    LPC_USB->HcControl = 0;
    LPC_USB->HcCommandStatus = CMD_HCR;
    for (i = 0; i < 1000 && (LPC_USB->HcCommandStatus & CMD_HCR); i++);

    LPC_USB->HcFmInterval    = 0x27782EDF;          // 1 ms frames, largest packet
    LPC_USB->HcPeriodicStart = 0x2A2F;              // 90 % of the frame
    LPC_USB->HcHCCA          = (uint32_t)hcca;
    LPC_USB->HcInterruptDisable = 0xFFFFFFFF;       // polled, no interrupts
    LPC_USB->HcInterruptStatus  = 0xFFFFFFFF;

    // empty control ed (head == tail)
    td_clear (&td[3]);
    ed_ctrl->Control = 8u << 16;
    ed_ctrl->TailTd  = (uint32_t)&td[3];
    ed_ctrl->HeadTd  = (uint32_t)&td[3];
    ed_ctrl->Next    = 0;
    LPC_USB->HcControlHeadED = (uint32_t)ed_ctrl;
    LPC_USB->HcBulkHeadED    = 0;

    LPC_USB->HcControl = CTRL_OPER | CTRL_CLE;      // running
    LPC_USB->HcRhStatus = RH_LPSC;                  // port power on
    LPC_USB->HcRhPortStatus1 = PORT_PPS;
    LPC_USB->HcRhPortStatus2 = PORT_PPS;
    portsc = 0;
    state = S_NONE;
}

void usbh_stop (void) {
    int_stop ();
    LPC_USB->HcRhPortStatus1 = PORT_LSDA;           // port power off
    LPC_USB->HcRhPortStatus2 = PORT_LSDA;
    LPC_USB->HcRhStatus = RH_LPS;
    LPC_USB->HcControl = 0;                         // back to reset state
    LPC_USB->HcInterruptStatus = 0xFFFFFFFF;
    LPC_USB->OTGStCtrl = 0;                         // port 1 back to device mode
    LPC_USB->OTGClkCtrl = 0;                        // clocks off
    state = S_NONE;
}

int usbh_poll (uint8_t *report) {
    uint32_t ps, i, cc;

    // no keyboard yet: look at both ports
    if (portsc == 0) {
        if (LPC_USB->HcRhPortStatus2 & PORT_CSC) LPC_USB->HcRhPortStatus2 = PORT_CSC;
        if (LPC_USB->HcRhPortStatus1 & PORT_CSC) LPC_USB->HcRhPortStatus1 = PORT_CSC;
        if      (LPC_USB->HcRhPortStatus2 & PORT_CCS) portsc = &LPC_USB->HcRhPortStatus2;
        else if (LPC_USB->HcRhPortStatus1 & PORT_CCS) portsc = &LPC_USB->HcRhPortStatus1;
        else return USBH_NO_DEV;
    }

    ps = PORTSC;
    if (ps & PORT_CSC) PORTSC = PORT_CSC;           // clear the change flag

    // unplugged
    if (!(ps & PORT_CCS)) {
        if (state != S_NONE) int_stop ();
        state = S_NONE;
        portsc = 0;
        return USBH_NO_DEV;
    }

    // just plugged in: let power settle, reset the port, enumerate
    if (state == S_NONE) {
        osDelay (100);
        port_reset ();
        state = (enumerate () == 0) ? S_READY : S_ERROR;
    }
    if (state == S_ERROR) return USBH_ERROR;

    // endpoint stalled: empty it and start again
    if (ed_int->HeadTd & 1u) {
        ed_int->HeadTd = ed_int->TailTd;
        int_queue ();
        return USBH_READY;
    }

    // a finished report?
    if ((ed_int->HeadTd & ~0xFu) == (ed_int->TailTd & ~0xFu)) {
        cc = int_cur->Control >> 28;
        if (cc == 0) {
            for (i = 0; i < 8; i++) report[i] = rep_buf[i];     // copy before the buffer is reused
            int_queue ();
            return USBH_REPORT;
        }
        int_queue ();
    }
    return USBH_READY;
}

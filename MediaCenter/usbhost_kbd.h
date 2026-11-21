// usbhost_kbd.h: minimal usb host (ohci) driver for one boot protocol keyboard
// jason su 501158090

#ifndef __USBHOST_KBD_H
#define __USBHOST_KBD_H

#include <stdint.h>

// usbh_poll results
#define USBH_NO_DEV   0     // nothing plugged in
#define USBH_READY    1     // keyboard enumerated, no new report
#define USBH_REPORT   2     // a new 8 byte report was copied out
#define USBH_ERROR    3     // device found but enumeration failed

void usbh_start (void);             // usb controller to host mode, port power on
void usbh_stop  (void);             // port power off, controller back to idle
int  usbh_poll  (uint8_t *report);  // call every ~10 ms from a thread

#endif

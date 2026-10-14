#pragma once

#include "game.h"

// The SD card, as a drive on the computer.
//
// A mode rather than an always-on feature, and the reason is the port. The Type-C is
// wired straight to the chip's native USB, so there is one interface to go round:
// putting TinyUSB on it takes the port away from the hardware USB-Serial/JTAG, which is
// the port esptool resets the chip through. Measured on the device: the drive appears
// and works, and while it is open there is no COM port at all. Left running always,
// flashing would need a BOOT-held power cycle every time. As a mode, that cost is only
// paid while it is open, and leaving it -- which reboots -- hands the port back.
//
// (Worth recording because it contradicts the core's own example: USBMSC.ino is guarded
// with a warning that it only works in OTG mode, and this board is built with
// USBMode=hwcdc. It works anyway. The warning is about the Arduino CDC, not about the
// mass-storage class.)
//
// The card is served at the block level -- the host's operating system does the file
// system, not this firmware -- so the computer sees the card itself with everything
// already on it. That also means the host caches: tapping to close before the computer
// has let go of the volume risks a half-written file, so the screen says so, and the
// device notices when the host ejects.
class GameUsbDrive : public Game {
public:
    const char *name() const override { return "USB DRIVE"; }
    uint16_t    accent() const override { return PX(RGB565(0x3F, 0xB6, 0xC0)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    enum State { ST_STARTING, ST_MOUNTED, ST_STOPPED, ST_NO_CARD };

    State state = ST_STOPPED;
    bool  started = false;       // did TinyUSB take the port
    bool  hostEjected = false;   // the computer said it was done with the volume
    int   sectors = 0;
    int   sectorSize = 512;

    void startMsc();
    // Stops the device and, if TinyUSB ever took the port, resets the chip: there is no
    // way back otherwise, and this ends in the state where a normal flash works again.
    void stopAndReboot();
};

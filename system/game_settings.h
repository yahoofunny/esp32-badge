#pragma once

#include "game.h"

// Settings: the volume, and what is under the hood.
//
// The volume is the assistant's, kept in the NVS key its own AudioCodec::Start() reads
// at boot ("audio"/"output_volume"). See audio.h for why that key rather than the
// codec's register: the register is rewritten by the assistant on every boot, so
// writing it reaches nothing.
//
// The other two pages are read-only. SYSTEM is the chip, memory and build; NET is what
// the assistant in the other slot is configured with -- its wifi, its server, the
// device id -- because that is the only network this board has, and this firmware never
// joins one. The page says so, rather than letting it read as a live link.
class GameSettings : public Game {
public:
    const char *name() const override { return "SETTINGS"; }
    uint16_t    accent() const override { return PX(RGB565(0xE0, 0xA8, 0x50)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;


private:
    enum Page { PAGE_MENU, PAGE_VOLUME, PAGE_SYSTEM, PAGE_NET, PAGE_USB };

    Page page = PAGE_MENU;

    // --- the volume page ---
    int  shown = -1;        // the assistant's level, from NVS
    int  raw = -1;          // the codec's register, for the readout
    bool saved = true;      // did the last write read back correctly
    bool dragging = false;

    void refresh();
    void apply(int percent);
    void enterPage(Page p);

    // Where a menu item sits, shared by the drawing and the hit test so the two cannot
    // disagree.
    void menuSlot(int i, float *sx, float *sy) const;

    void updateMenu();
    void updateVolume();
    void updateInfo();      // the read-only pages: only BACK accepts a tap
    void updateUsb();

    void renderMenu();
    void renderVolume();
    void renderSystem();
    void renderNet();
    void renderUsb();

    // The way back, at the top centre. Not the top-left: at that height the round bezel
    // leaves nothing visible out there.
    void renderBack();

    // --- the USB drive page ---------------------------------------------------------
    // The SD card handed to the host as a block device. It lives here rather than as a
    // carousel entry: it is not something to play, and a mode that takes the board's only
    // USB port away from the serial console is not something to fall into by accident.
    enum UsbState { USB_STOPPED, USB_MOUNTED, USB_NO_CARD };
    UsbState usbState = USB_STOPPED;
    bool usbStarted = false;      // did TinyUSB actually take the port
    bool usbHostEjected = false;  // the computer said it was done with the volume
    int  usbSectors = 0;
    int  usbSectorSize = 512;
    float usbIdleSec = 0.0f;      // since the last thing the host asked for
    float usbEjectedSec = 0.0f;

    void usbStart();
    // Stops the device and, if TinyUSB ever took the port, resets the chip. There is no
    // way back otherwise: the hardware USB-Serial/JTAG shares the interface with it, and
    // that is the port esptool resets through. Rebooting hands it back.
    void usbStopAndReboot();
};

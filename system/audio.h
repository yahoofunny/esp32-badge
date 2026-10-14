#pragma once

#include <stdint.h>

// The master volume, which on this board lives in the ES8311 codec rather than in
// whichever firmware is running.
//
// The codec has no reset line here: docs/hardware.md lists the I2S pins and an
// amplifier enable on GPIO9 and nothing else. So its registers survive a CPU reset
// and a slot switch, and one level covers both firmwares. That is why the stock
// system's settings screen could control the assistant's volume, and why a control
// here can too.
//
// The one thing that would break it is the assistant writing the register itself on
// boot. It is printed at startup so that shows up rather than being guessed at.
namespace audio {

// 0x18, not 0x30. The datasheet quotes the address in eight-bit form -- 0x30 to write
// and 0x31 to read -- and 0x30 is what docs/hardware.md had, because that is what the
// chip's own documentation says. Arduino's Wire takes the seven-bit form, so every
// transaction to 0x30 went to a device that is not there and the codec simply never
// answered. An I2C scan of the running board settles it: 0x15 touch, 0x18 codec,
// 0x40 microphone array, 0x51 RTC, 0x55 gauge, 0x6B IMU.
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t REG_DAC_VOLUME = 0x32;

// The register's own scale: 0x00 is about -95 dB, 0xFF is 0 dB.
constexpr int VOLUME_MIN = 0;
constexpr int VOLUME_MAX = 100;
constexpr int VOLUME_STEP = 10;

// The level the codec is holding, as a percentage, or -1 if it did not answer.
int volume();

// Writes the level into the codec. Returns false if the codec did not acknowledge.
bool setVolume(int percent);

// Reads the register back after writing, so a silent failure cannot look like success.
bool verify(int percent);

// The codec's raw 0x32 register, or -1. Worth showing on screen: it is the value both
// firmwares are sharing, and there is no other way to see it.
int rawRegister();

// The level is remembered across a power cycle, where the codec's own registers are
// not: with the battery out the codec comes up at its power-on default. So the value
// is kept in NVS as well and written back into the codec at startup.
int  storedVolume();              // -1 when nothing has been stored
void storeVolume(int percent);
void applyStored();               // no-op when nothing has been stored

// --- the assistant's level ------------------------------------------------------
//
// This is the one that matters, and it is not the codec register. The assistant in
// the other slot writes that register itself, every time it boots, from a level it
// reads out of NVS:
//
//     AudioCodec::Start():  output_volume_ = settings.GetInt("output_volume", 70);
//     (namespace "audio", one int32, defaulting to 70 when the key is absent)
//
// Measured on the device: a value written to the codec alone came back changed after
// the assistant had booted, and a dump of the whole NVS partition showed no "audio"
// namespace at all -- because nothing had ever set one. So writing that key is the
// way in, and it is the assistant's own documented interface rather than a trick on
// top of it: its SetOutputVolume persists to the same place.
//
// Its own range is 0..100 and it treats 0 as 10, so the floor here is 10.
constexpr int ASSISTANT_MIN = 10;
constexpr int ASSISTANT_DEFAULT = 70;    // what it uses when the key is absent

int  assistantVolume();                  // NVS value, or ASSISTANT_DEFAULT
bool setAssistantVolume(int percent);    // writes and reads back

}  // namespace audio

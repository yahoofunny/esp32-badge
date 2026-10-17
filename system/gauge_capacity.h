#pragma once
#include <stdint.h>
#include <string.h>

// BQ27220 TRM chapter 6.1: change the RAM copy through MACData, never OTP.
// 0x929d contains profile-1 FCC, followed by design capacity, both big-endian.
namespace power {
class GaugeConfigIo {
public:
    virtual ~GaugeConfigIo() = default;
    virtual bool read(uint8_t reg, uint8_t *data, uint8_t size) = 0;
    virtual bool write(uint8_t reg, const uint8_t *data, uint8_t size) = 0;
    virtual void wait(uint32_t ms) = 0;
    virtual bool backup(const uint8_t *data, uint8_t size) = 0;
};

inline uint16_t gaugeLe(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint16_t gaugeBe(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
inline uint8_t gaugeChecksum(const uint8_t *data, uint8_t size) {
    uint8_t sum = uint8_t(0x9d + 0x92);
    for (uint8_t i = 0; i < size; ++i) sum += data[i];
    return uint8_t(0xff - sum);
}
inline bool gaugeControl(GaugeConfigIo &io, uint16_t cmd) {
    const uint8_t bytes[] = {uint8_t(cmd),uint8_t(cmd >> 8)};
    return io.write(0x00,bytes,2);
}
inline bool gaugeKeys(GaugeConfigIo &io, uint16_t first, uint16_t second) {
    if (!gaugeControl(io,first)) return false;
    io.wait(10);
    if (!gaugeControl(io,second)) return false;
    io.wait(20); return true;
}
inline bool gaugeWord(GaugeConfigIo &io, uint8_t reg, uint16_t &word) {
    uint8_t bytes[2];
    if (!io.read(reg,bytes,2)) return false;
    word = gaugeLe(bytes); return true;
}
inline bool gaugeWaitFlag(GaugeConfigIo &io, uint16_t mask, uint16_t value) {
    for (int i = 0; i < 30; ++i) {
        uint16_t status = 0;
        if (gaugeWord(io,0x3a,status) && (status & mask) == value) return true;
        io.wait(100);
    }
    return false;
}
inline bool gaugeSelectCapacity(GaugeConfigIo &io) {
    const uint8_t address[] = {0x9d,0x92};
    if (!io.write(0x3e,address,2)) return false;
    io.wait(10); return true;
}
inline bool gaugeReadCapacityBlock(GaugeConfigIo &io, uint8_t *data) {
    uint8_t echo[2],sumLen[2];
    return gaugeSelectCapacity(io) && io.read(0x3e,echo,2) &&
        echo[0] == 0x9d && echo[1] == 0x92 && io.read(0x40,data,32) &&
        io.read(0x60,sumLen,2) && sumLen[1] == 36 &&
        sumLen[0] == gaugeChecksum(data,32);
}
inline bool gaugeWriteCapacityBlock(GaugeConfigIo &io, const uint8_t *data) {
    const uint8_t commit[] = {gaugeChecksum(data,32),36};
    if (!gaugeSelectCapacity(io) || !io.write(0x40,data,32) ||
        !io.write(0x60,commit,2)) return false;
    io.wait(20);
    uint8_t verify[32];
    return gaugeReadCapacityBlock(io,verify) && memcmp(data,verify,32) == 0;
}

enum class CapacityResult { Unchanged, Updated, Invalid, AccessFailed, ConfigFailed,
    BackupFailed, WriteFailed, RestoreFailed, ExitFailed, VerifyFailed };

inline CapacityResult configureGaugeCapacity(GaugeConfigIo &io, uint16_t mah) {
    if (mah < 100 || mah > 10000) return CapacityResult::Invalid;
    uint16_t status = 0,design = 0,full = 0;
    if (!gaugeWord(io,0x3a,status) || (status & 0x0401) ||
        !gaugeWord(io,0x3c,design) || !gaugeWord(io,0x12,full))
        return CapacityResult::AccessFailed;
    // Keep a learned FCC. A new battery must be explicitly configured again.
    if (design == mah && full > 0 && full <= uint32_t(mah) * 3 / 2)
        return CapacityResult::Unchanged;
    if ((status & 6) == 6) {
        if (!gaugeKeys(io,0x0414,0x3672))
            return CapacityResult::AccessFailed;
        if (!gaugeWord(io,0x3a,status)) return CapacityResult::AccessFailed;
        // Some ROM versions document the alternative default unseal key.
        if ((status & 6) == 6) {
            if (!gaugeKeys(io,0x8000,0x8000))
                return CapacityResult::AccessFailed;
        }
    }
    if (!gaugeKeys(io,0xffff,0xffff) ||
        !gaugeWaitFlag(io,6,2)) {
        gaugeControl(io,0x0030); return CapacityResult::AccessFailed;
    }
    const bool entered = gaugeControl(io,0x0090);
    io.wait(2000); // TRM 2.2.21 requires at least 2 seconds before flag reads.
    CapacityResult result = CapacityResult::ConfigFailed;
    bool changed = false;
    if (entered && gaugeWaitFlag(io,0x0400,0x0400)) {
        uint8_t old[32],next[32];
        if (!gaugeReadCapacityBlock(io,old)) result = CapacityResult::BackupFailed;
        else if (gaugeBe(old) != full || gaugeBe(old+2) != design)
            result = CapacityResult::BackupFailed; // refuse an unexpected profile.
        else if (!io.backup(old,32)) result = CapacityResult::BackupFailed;
        else {
            memcpy(next,old,32);
            next[0] = next[2] = uint8_t(mah >> 8);
            next[1] = next[3] = uint8_t(mah);
            if (gaugeWriteCapacityBlock(io,next)) {
                result = CapacityResult::Updated; changed = true;
            } else {
                result = gaugeWriteCapacityBlock(io,old) ? CapacityResult::WriteFailed
                                                        : CapacityResult::RestoreFailed;
            }
        }
    }
    // Always exit even on a failed write; normal gauging must resume.
    const bool exited = gaugeControl(io,changed ? 0x0091 : 0x0092) &&
        gaugeWaitFlag(io,0x0400,0);
    const bool sealed = gaugeControl(io,0x0030) && gaugeWaitFlag(io,6,6);
    if (!exited || !sealed) return CapacityResult::ExitFailed;
    if (!changed) return result;
    io.wait(2000);
    if (!gaugeWord(io,0x3c,design) || !gaugeWord(io,0x12,full) ||
        design != mah || full != mah) return CapacityResult::VerifyFailed;
    return CapacityResult::Updated;
}
} // namespace power

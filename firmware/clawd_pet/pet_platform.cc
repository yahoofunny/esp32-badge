#include "pet_platform.h"
#include "input.h"
#include "clock.h"
#include "sound.h"
#include "application.h"
#include "assets/lang_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctime>
#include <cstring>
#include <sys/time.h>

namespace {
i2c_master_dev_handle_t touch, imu, rtc;
bool down = false, tap = false;
int tx = 0, ty = 0;
uint32_t heldSince = 0;
float accel[3] = {0, 0, 1};
bool imuReady = false;
uint32_t imuEnabledAt = 0;
uint32_t nowMs() { return uint32_t(esp_timer_get_time() / 1000); }
bool readReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t n) {
    // IDF runs at 100Hz here: an 8ms timeout rounds down to zero ticks and
    // returns before the I2C completion interrupt. Use a real bounded wait.
    return dev && i2c_master_transmit_receive(dev, &reg, 1, data, n, 50) == ESP_OK;
}
bool writeReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return dev && i2c_master_transmit(dev, data, 2, 50) == ESP_OK;
}
void add(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t *dev) {
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, dev));
}
int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
}

void clawd::BeginPlatform(i2c_master_bus_handle_t bus) {
    add(bus, 0x15, &touch); add(bus, 0x6b, &imu); add(bus, 0x51, &rtc);
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_1, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_NUM_1, 1); vTaskDelay(pdMS_TO_TICKS(50));
    const bool touchReady = writeReg(touch, 0xfe, 1);
    uint8_t touchId = 0;
    readReg(touch, 0xa7, &touchId, 1);
    ESP_LOGI("pet", "CST816 id=0x%02x ready=%d", touchId, touchReady);
    // Like SensorLib, reset the IMU before checking WHOAMI. The launcher may have
    // left it sampling with a different configuration across the firmware reboot.
    writeReg(imu, 0x60, 0xb0);
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t id = 0, reset = 0;
    for (int i=0; i<25; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (readReg(imu, 0x4d, &reset, 1) && reset == 0x80) break;
    }
    if (reset == 0x80 && readReg(imu, 0, &id, 1) && id == 5) {
        // Waveshare's IDF QMI8658 example: normal 6DOF, 8g accelerometer.
        imuReady = writeReg(imu, 0x02, 0x60) &&
            writeReg(imu, 0x03, 0x23) &&
            writeReg(imu, 0x04, 0x43) &&
            writeReg(imu, 0x08, 0x03);
        imuEnabledAt = nowMs();
        ESP_LOGI("pet", "QMI8658 id=0x%02x reset=0x%02x ready=%d", id, reset, imuReady);
    } else { ESP_LOGW("pet", "QMI8658 unavailable: id=0x%02x reset=0x%02x", id, reset); }
    setenv("TZ", "CST-8", 1); tzset();
    uint8_t r[7];
    if (readReg(rtc, 4, r, sizeof(r)) && !(r[0] & 0x80)) {
        tm t{}; t.tm_sec=bcd(r[0]&0x7f);t.tm_min=bcd(r[1]&0x7f);
        t.tm_hour=bcd(r[2]&0x3f);t.tm_mday=bcd(r[3]&0x3f);
        t.tm_mon=bcd(r[5]&0x1f)-1;t.tm_year=bcd(r[6])+100;t.tm_isdst=-1;
        if (t.tm_year >= 125 && t.tm_mon >= 0 && t.tm_mon < 12 && t.tm_mday > 0 &&
            t.tm_mday <= 31 && t.tm_hour < 24 && t.tm_min < 60 && t.tm_sec < 60) {
            timeval tv{mktime(&t),0}; settimeofday(&tv,nullptr);
        }
    }
}

void clawd::PollInput() {
    tap = false;
    uint8_t p[5];
    if (readReg(touch, 2, p, sizeof(p))) {
        const bool next = (p[0] & 15) != 0;
        if (next) {
            tx = ((p[1]&15)<<8)|p[2]; ty=((p[3]&15)<<8)|p[4];
            if (!down) {
                heldSince=nowMs();
                ESP_LOGD("pet-input", "down %d,%d", tx, ty);
            }
        } else if (down) {
            tap=true;
            ESP_LOGD("pet-input", "release %d,%d held=%lu", tx, ty, (unsigned long)(nowMs()-heldSince));
        }
        down=next;
    }
    static uint32_t last_imu=0;
    const uint32_t now=nowMs();
    if(uint32_t(now-last_imu)>=50) {
        last_imu=now;
        uint8_t a[6];
        if (imuReady && readReg(imu, 0x35, a, sizeof(a))) {
            for (int i=0;i<3;++i) accel[i]=int16_t(a[i*2]|(a[i*2+1]<<8))*(8.0f/32768);
            static bool reported = false;
            // A successful I2C read immediately after enabling can still contain
            // the reset value. Diagnose after the sensor's startup has settled.
            if (!reported && uint32_t(now-imuEnabledAt)>=1000) {
                uint8_t ctrl[8] = {}, status = 0;
                readReg(imu, 0x02, ctrl, sizeof(ctrl));
                readReg(imu, 0x2e, &status, 1);
                ESP_LOGI("pet", "QMI8658 sample: x=%.3fg y=%.3fg z=%.3fg ctrl=%02x/%02x/%02x/%02x/%02x status=%02x",
                    accel[0], accel[1], accel[2], ctrl[0], ctrl[1], ctrl[4], ctrl[6], ctrl[7], status);
                reported = true;
            }
        }
    }
}

void clawd::ReturnToLauncher() {
    const auto *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (!part || esp_ota_set_boot_partition(part) != ESP_OK) return;
    esp_restart();
}

namespace input {
bool init(){return true;} void update(float){}
float tiltX(){return -accel[0];} float tiltY(){return accel[1];}
void readRawAccel(float out[3]){memcpy(out,accel,sizeof(accel));}
bool touchDown(){return down;} bool tapped(){return tap;}
int tapX(){return tx;} int tapY(){return ty;}
unsigned long touchHeldMs(){return down ? nowMs()-heldSince : 0;}
bool bootDown(){return gpio_get_level(GPIO_NUM_0)==0;} unsigned long bootHeldMs(){return 0;}
}
namespace sysclock {
bool valid(){return time(nullptr)>1735689600;}
const char *timeText(){static char buf[6];time_t n=time(nullptr);tm t{};localtime_r(&n,&t);
    if(valid())strftime(buf,sizeof(buf),"%H:%M",&t);else strcpy(buf,"--:--");return buf;}
}
namespace sound {
void play2(int,int,int,int){
    auto& app=Application::GetInstance();
    if(app.GetDeviceState()==kDeviceStateIdle)
        app.Schedule([](){Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);});
}
void nope(){} // Voice owns the only I2S peripheral; don't start a second synthesizer.
}

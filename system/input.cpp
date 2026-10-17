#include "input.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "I2C_Driver.h"
#include "SensorQMI8658.hpp"
#include "Touch_CST816.h"

namespace input {

// Fixed axis mapping for this board, measured on hardware with it flat and the
// screen up: axis 2 (z) is perpendicular to the screen, and the in-plane axes are
// 0 (x) and 1 (y) in that order, with X inverted and Y direct.
//
// This used to be auto-detected at boot by finding the axis carrying the most
// gravity. That is fragile in a way that matters: the sample window is short, and
// whatever the board happens to be doing during it decides the result. Pick the
// board up while it boots and the mapping comes out wrong, which then makes the tilt
// control behave nonsense. The mounting is a property of the hardware, not of the
// boot-time pose, so it is simply stated here.
static const int PERP_AXIS = 2;
static const int AXIS_X    = 0;
static const int AXIS_Y    = 1;
static const int SIGN_X    = -1;
static const int SIGN_Y    = +1;

static const int   BOOT_PIN = 0;
static const float TRIM_GATE  = 0.16f;   // g; only trim within this band around level
static const float TRIM_ALPHA = 0.005f;  // per frame -> ~3.3 s time constant at 60 fps
static const float DEADZONE   = 0.025f;  // g; applied after trimming

static SensorQMI8658 imu;
static bool  imuOk = false;
static float latestAccel[3]={};
static bool latestAccelValid=false;


static float tiltXv = 0.0f, tiltYv = 0.0f;
static float trimX = 0.0f, trimY = 0.0f;

static bool     down = false;
static bool     tapFlag = false;
static int      tapXv = 0, tapYv = 0;
static uint32_t downSince = 0;
static uint32_t bootSince = 0;

static bool readAccel(float *out)
{
    if (!imuOk || !imu.getAccelerometer(out[0], out[1], out[2])) {
        out[0] = out[1] = out[2] = 0.0f;
        return false;
    }
    return isfinite(out[0])&&isfinite(out[1])&&isfinite(out[2])&&
           out[0]*out[0]+out[1]*out[1]+out[2]*out[2]>.01f;
}

static void reportOrientation()
{
    // Read a short average purely for the log. It is not used to derive anything --
    // if the magnitude of the perpendicular axis is nowhere near 1 g, the board was
    // being moved while this ran, which is useful to know when a tilt feels wrong.
    const int N = 40;
    float sum[3] = {0, 0, 0};
    for (int i = 0; i < N; i++) {
        float a[3];
        readAccel(a);
        sum[0] += a[0]; sum[1] += a[1]; sum[2] += a[2];
        delay(4);
    }
    float avg[3];
    for (int i = 0; i < 3; i++) avg[i] = sum[i] / N;

    Serial.printf("[input] boot average x=%.3f y=%.3f z=%.3f g (perpendicular axis |z|, "
                  "should be near 1.0 if the board was still)\n", avg[0], avg[1], avg[2]);
    Serial.printf("[input] mapping: screen X <- axis %d * %+d, screen Y <- axis %d * %+d "
                  "(perpendicular axis %d)\n",
                  AXIS_X, SIGN_X, AXIS_Y, SIGN_Y, PERP_AXIS);
}

bool init()
{
    pinMode(BOOT_PIN, INPUT_PULLUP);

    I2C_Init();
    Wire.setClock(400000);              // touch and IMU share this bus

    // Touch_Init() (run by LCD_Init) leaves the controller's auto-sleep enabled,
    // and a sleeping CST816 does not answer I2C -- polling it every frame then
    // produces a steady stream of NACKs whenever nobody is touching the screen.
    Touch_SetAutoSleep(false);

    imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS);
    if (imuOk) {
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                SensorQMI8658::ACC_ODR_250Hz,
                                SensorQMI8658::LPF_MODE_3);
        imu.enableAccelerometer();
        Serial.println("[input] QMI8658 ready");
        reportOrientation();
    } else {
        Serial.println("[input] QMI8658 NOT found -- tilt input disabled");
    }
    return imuOk;
}

void update(float dt)
{
    (void)dt;

    float a[3];
    latestAccelValid=readAccel(a);
    for(int i=0;i<3;++i)latestAccel[i]=a[i];

    const float rx = a[AXIS_X] * SIGN_X;
    const float ry = a[AXIS_Y] * SIGN_Y;

    if (fabsf(rx - trimX) < TRIM_GATE && fabsf(ry - trimY) < TRIM_GATE) {
        trimX += (rx - trimX) * TRIM_ALPHA;
        trimY += (ry - trimY) * TRIM_ALPHA;
    }

    float tx = rx - trimX;
    float ty = ry - trimY;
    if (fabsf(tx) < DEADZONE) tx = 0.0f;
    if (fabsf(ty) < DEADZONE) ty = 0.0f;
    tiltXv = tx;
    tiltYv = ty;

    // --- touch ---------------------------------------------------------------
    // Touch_Read_Data() only assigns `points` when the controller reports a
    // non-zero count, so it never falls back to zero on its own. Clearing it here
    // after consuming the sample is what makes release detection work at all.
    tapFlag = false;
    Touch_Read_Data();
    const bool nowDown = (touch_data.points != 0);
    if (nowDown) {
        tapXv = touch_data.x;
        tapYv = touch_data.y;
    }
    touch_data.points = 0;
    touch_data.gesture = NONE;

    if (nowDown && !down) {
        downSince = millis();
    } else if (!nowDown && down) {
        tapFlag = true;                 // released this frame
    }
    down = nowDown;

    // --- BOOT button ---------------------------------------------------------
    if (digitalRead(BOOT_PIN) == LOW) {
        if (bootSince == 0) bootSince = millis();
    } else {
        bootSince = 0;
    }
}

float tiltX() { return tiltXv; }
float tiltY() { return tiltYv; }

void readRawAccel(float out[3])
{
    readAccel(out);
}
bool latestRawAccel(float out[3]) {
    for(int i=0;i<3;++i)out[i]=latestAccel[i];
    return latestAccelValid;
}

bool touchDown() { return down; }
bool tapped()    { return tapFlag; }
int  tapX()      { return tapXv; }
int  tapY()      { return tapYv; }
unsigned long touchHeldMs() { return down ? (millis() - downSince) : 0; }

bool bootDown() { return bootSince != 0; }
unsigned long bootHeldMs() { return bootSince ? (millis() - bootSince) : 0; }

}  // namespace input

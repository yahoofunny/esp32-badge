/*
 * clawd-roll — a tilt-controlled rolling clawd.
 *
 * Tilting the board accelerates clawd; clawd rolls, bounces off the circular arena
 * wall with restitution, and spins in proportion to its speed. There is no goal or
 * score — it is just a thing that feels good to hold and wobble.
 *
 * Rendering bypasses LVGL entirely: the sketch owns one full-screen RGB565
 * framebuffer in PSRAM and draws into it directly. That keeps the frame loop cheap
 * and sidesteps the LVGL 8.x/9.x API split that the two Waveshare example trees
 * disagree about.
 *
 * The panel expects big-endian RGB565 and LCD_pushFull() does not byte-swap, so
 * every pixel is stored pre-swapped through PIXEL(). See Display_ST77916.cpp for
 * why LCD_addWindow() cannot be used here.
 *
 * Board: Waveshare ESP32-S3-Touch-LCD-1.85B (ESP32-S3R8, 360x360 ST77916 QSPI,
 * QMI8658 IMU on the shared I2C bus).
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Display_ST77916.h"
#include "I2C_Driver.h"
#include "SensorQMI8658.hpp"
#include "clawd_asset.h"

// ---------------------------------------------------------------- geometry --
static const int   SCR_W = 360;
static const int   SCR_H = 360;
static const float CX    = 180.0f;          // arena centre
static const float CY    = 180.0f;
static const float ARENA = 172.0f;          // arena radius (inner edge of the wall)

// clawd's collision radius on screen. The sprite is scaled so that its
// circumradius (half its diagonal) equals this, which is what makes the spin look
// like it is pivoting on the arena floor instead of clipping through the wall.
static const float BALL_R = 54.0f;

// Source sprite geometry, used to convert BALL_R into a scale factor.
static const float SPRITE_CIRCUMR =
    sqrtf((CLAWD_W * 0.5f) * (CLAWD_W * 0.5f) + (CLAWD_H * 0.5f) * (CLAWD_H * 0.5f));

// ------------------------------------------------------------------ colours --
// Panel byte order: big-endian RGB565. PIXEL() wraps every constant so nothing is
// ever written in host order by accident.
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define BSWAP16(c)      ((uint16_t)((((uint16_t)(c) & 0x00FF) << 8) | (((uint16_t)(c) & 0xFF00) >> 8)))
#define PIXEL(c)        BSWAP16(c)

static const uint16_t COL_BG      = PIXEL(RGB565(0x0B, 0x0B, 0x12));
static const uint16_t COL_OUTSIDE = PIXEL(RGB565(0x06, 0x06, 0x0A));
static const uint16_t COL_RING    = PIXEL(RGB565(0x5A, 0x5A, 0xC8));
static const uint16_t COL_GLOW    = PIXEL(RGB565(0x24, 0x24, 0x52));
static const uint16_t COL_DOT     = PIXEL(RGB565(0x1A, 0x1A, 0x2E));
static const uint16_t COL_IMPACT  = PIXEL(RGB565(0xFF, 0xD2, 0x3E));

static const uint16_t COL_BODY    = PIXEL(CLAWD_BODY_RGB565);
static const uint16_t COL_DETAIL  = PIXEL(CLAWD_DETAIL_RGB565);

// ------------------------------------------------------------------- physics --
// ACC_GAIN is the main feel knob. Terminal speed at a given tilt works out to
// ACC_GAIN * tilt[g] / (1 - FRICTION); with these values a gentle ~9 degree tilt
// settles around a third of MAX_SPEED and a firm ~30 degree tilt saturates it.
static const float ACC_GAIN  = 0.40f;
static const float FRICTION  = 0.985f;  // per-frame velocity decay (long, floaty coast)
static const float MAX_SPEED = 9.0f;    // px per frame
static const float RESTITUTION = 0.86f; // wall bounciness
static const float DEADZONE  = 0.025f;  // g; applied after trimming

// Slow auto-trim of the accelerometer's static offset.
//
// Because of friction, *any* steady acceleration produces a steady terminal
// velocity -- so a bias of even 0.06 g is enough to walk the ball into a wall and
// hold it there, which reads as "it only sticks to the edge". Real accelerometers
// carry exactly that much zero-g offset, and a badge resting on a slightly sloped
// surface adds more.
//
// Trimming only happens while the board is already close to level, so it removes
// that offset without ever fighting a deliberate tilt: lean the board and the gate
// closes, leaving the player's input untouched.
static const float TRIM_GATE  = 0.16f;   // g; only trim within this band around level
static const float TRIM_ALPHA = 0.005f;  // per frame -> ~3.3 s time constant at 60 fps
static float trimX = 0.0f, trimY = 0.0f;

// Exit gesture. The hold time is deliberately long, and the gesture stays disarmed
// for the first few seconds, so a floating or mis-read GPIO0 cannot eject the user
// back to the stock system before the game has even appeared.
static const unsigned long EXIT_ARM_DELAY_MS = 3000;
static const unsigned long EXIT_HOLD_MS      = 2000;

// -------------------------------------------------------------- IMU mapping ---
// Which in-plane accelerometer axis drives screen X and screen Y, and with which
// sign. The axis perpendicular to the screen is found automatically at boot (it is
// the one carrying gravity when the board lies flat), so only the in-plane
// assignment has to be established empirically.
//
// Verified on hardware: with the board flat and the screen up, the perpendicular
// axis is z, the in-plane axes are x and y in that order, Y maps directly, and X is
// inverted -- tilting the board to the right has to send clawd to the right, which
// is the opposite of what the raw axis reports.
static const int AXIS_X = 0;
static const int AXIS_Y = 1;
static const int SIGN_X = -1;
static const int SIGN_Y = +1;

// ---------------------------------------------------------------- rendering --
static uint16_t *fb   = nullptr;   // working framebuffer, panel byte order
static uint16_t *bg   = nullptr;   // static backdrop (arena), restored per frame

// ------------------------------------------------------------------- state ----
static float bx = CX, by = CY - 90.0f;
static float vx = 0.0f, vy = 0.0f;
static float roll = 0.0f;

static int   prevY0 = 0, prevY1 = 0;   // sprite bbox rows touched last frame

static float impactX = 0, impactY = 0, impactAge = 0.0f;
static const float IMPACT_LIFE = 0.30f;

static SensorQMI8658 imu;
static bool  imuOk = false;
static int   gravityAxis = 2;          // axis perpendicular to the screen (auto)
static int   inPlane[2]  = {0, 1};     // the other two axes, ascending

static unsigned long lastFrameUs = 0;
static unsigned long lastDiagMs  = 0;

// ============================================================== background ====
static void buildBackground()
{
  for (int y = 0; y < SCR_H; y++) {
    for (int x = 0; x < SCR_W; x++) {
      const float dx = (x + 0.5f) - CX;
      const float dy = (y + 0.5f) - CY;
      const float d  = sqrtf(dx * dx + dy * dy);

      uint16_t c;
      if (d > ARENA + 6.0f) {
        c = COL_OUTSIDE;                       // dark band beyond the wall
      } else if (d > ARENA - 1.5f) {
        c = COL_RING;                          // the wall itself
      } else if (d > ARENA - 6.0f) {
        c = COL_GLOW;                          // inner glow, sells the edge
      } else if (((x % 45) == 22) && ((y % 45) == 22)) {
        c = COL_DOT;                           // sparse floor dots give motion reference
      } else {
        c = COL_BG;
      }
      bg[y * SCR_W + x] = c;
    }
  }
}

// ================================================================= sprite =====
static inline uint8_t spriteIndex(int sx, int sy)
{
  return (clawd_bits[sy * CLAWD_ROW_BYTES + (sx >> 2)] >> ((sx & 3) * 2)) & 0x03;
}

static void drawClawd(float ca, float sa)
{
  const float inv = SPRITE_CIRCUMR / BALL_R;   // dest px -> source px

  int x0 = (int)floorf(bx - BALL_R); if (x0 < 0) x0 = 0;
  int x1 = (int)ceilf (bx + BALL_R); if (x1 > SCR_W - 1) x1 = SCR_W - 1;
  int y0 = (int)floorf(by - BALL_R); if (y0 < 0) y0 = 0;
  int y1 = (int)ceilf (by + BALL_R); if (y1 > SCR_H - 1) y1 = SCR_H - 1;

  prevY0 = y0; prevY1 = y1;

  for (int py = y0; py <= y1; py++) {
    const float dy = (py + 0.5f) - by;
    uint16_t *row = fb + (size_t)py * SCR_W;
    for (int px = x0; px <= x1; px++) {
      const float dx = (px + 0.5f) - bx;

      // inverse rotation + scale, into source pixel coordinates
      const float rx =  dx * ca + dy * sa;
      const float ry = -dx * sa + dy * ca;
      const int sx = (int)(rx * inv + CLAWD_W * 0.5f);
      const int sy = (int)(ry * inv + CLAWD_H * 0.5f);

      if (sx < 0 || sx >= CLAWD_W || sy < 0 || sy >= CLAWD_H) continue;

      const uint8_t idx = spriteIndex(sx, sy);
      if (idx == 0) continue;                  // transparent
      row[px] = (idx == 1) ? COL_BODY : COL_DETAIL;
    }
  }
}

// Expanding ring drawn where clawd hits the wall.
static void drawImpact()
{
  if (impactAge <= 0.0f) return;

  const float t = 1.0f - (impactAge / IMPACT_LIFE);   // 0 -> 1 over the lifetime
  const float r = 6.0f + t * 46.0f;
  const float thickness = 2.5f * (1.0f - t) + 0.9f;

  int x0 = (int)(impactX - r - 3); if (x0 < 0) x0 = 0;
  int x1 = (int)(impactX + r + 3); if (x1 > SCR_W - 1) x1 = SCR_W - 1;
  int y0 = (int)(impactY - r - 3); if (y0 < 0) y0 = 0;
  int y1 = (int)(impactY + r + 3); if (y1 > SCR_H - 1) y1 = SCR_H - 1;

  const float rr = r * r;
  for (int py = y0; py <= y1; py++) {
    const float dy = (py + 0.5f) - impactY;
    uint16_t *row = fb + (size_t)py * SCR_W;
    for (int px = x0; px <= x1; px++) {
      const float dx = (px + 0.5f) - impactX;
      const float d2 = dx * dx + dy * dy;
      if (d2 > rr) continue;
      const float dr = sqrtf(d2) - r;
      if (dr > thickness || dr < -thickness) continue;
      row[px] = COL_IMPACT;
    }
  }
}

// =================================================================== IMU ======
static void readAccel(float *out)
{
  if (!imuOk) { out[0] = out[1] = out[2] = 0.0f; return; }
  imu.getAccelerometer(out[0], out[1], out[2]);
}

static void detectOrientation()
{
  // Average a short window, then treat the axis holding the largest magnitude as
  // the one perpendicular to the screen. Everything else follows from that, so the
  // user never has to declare how the chip is mounted.
  //
  // The averaged vector is used ONLY to identify that axis. It is deliberately not
  // kept as a "neutral" to subtract at runtime: with a gravity-sensed tilt control,
  // level *is* zero on the in-plane axes, so subtracting a snapshot taken at boot
  // merely bakes in whatever angle the board happened to be held at. A constant
  // residual tilt like that pins the ball against the wall and it never comes back.
  const int N = 40;
  float sum[3] = {0, 0, 0};
  for (int i = 0; i < N; i++) {
    float a[3];
    readAccel(a);
    sum[0] += a[0]; sum[1] += a[1]; sum[2] += a[2];
    delay(5);
  }
  float avg[3];
  for (int i = 0; i < 3; i++) avg[i] = sum[i] / N;

  gravityAxis = 0;
  for (int i = 1; i < 3; i++) {
    if (fabsf(avg[i]) > fabsf(avg[gravityAxis])) gravityAxis = i;
  }
  int k = 0;
  for (int i = 0; i < 3; i++) {
    if (i != gravityAxis) inPlane[k++] = i;
  }

  Serial.printf("[imu] boot average x=%.3f y=%.3f z=%.3f g (used only to find the "
                "perpendicular axis)\n", avg[0], avg[1], avg[2]);
  Serial.printf("[imu] gravity axis = %d (%s)  in-plane axes = %d, %d\n",
                gravityAxis, "xyz" + gravityAxis, inPlane[0], inPlane[1]);
  Serial.printf("[imu] screen X <- axis %d * %+d, screen Y <- axis %d * %+d\n",
                inPlane[AXIS_X], SIGN_X, inPlane[AXIS_Y], SIGN_Y);
}

// ====================================================== switch back to stock ===
static void returnToStockSystem()
{
  const esp_partition_t *factory = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (!factory) {
    Serial.println("[exit] no factory partition in the table; cannot switch back");
    return;
  }
  Serial.println("[exit] switching boot partition to factory, restarting...");
  esp_ota_set_boot_partition(factory);
  delay(150);
  esp_restart();
}

// Long-press BOOT (GPIO0) to leave the game. GPIO0 is a strapping pin, so it is
// only sampled here at runtime -- never at reset, or the board would come up in
// the ROM download loader instead of running this sketch.
//
// The minimum-uptime guard exists because an exit that fires during boot is
// indistinguishable from a boot failure: it erases otadata and restarts, leaving
// the board on the stock system with no trace of what happened.
static unsigned long bootMs = 0;

static void pollExitButton()
{
  static unsigned long heldSince = 0;

  if (millis() - bootMs < EXIT_ARM_DELAY_MS) return;   // not armed yet

  if (digitalRead(0) == LOW) {
    if (heldSince == 0) heldSince = millis();
    if (millis() - heldSince > EXIT_HOLD_MS) returnToStockSystem();
  } else {
    heldSince = 0;
  }
}

// ================================================================= setup ======
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd-roll] booting");

  pinMode(0, INPUT_PULLUP);           // BOOT button -> exit to the stock system

  I2C_Init();
  Wire.setClock(400000);              // touch + IMU share this bus

  fb = (uint16_t *)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
  bg = (uint16_t *)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
  if (!fb || !bg) {
    Serial.println("[clawd-roll] FATAL: framebuffer allocation failed");
    while (1) delay(1000);
  }
  Serial.printf("[clawd-roll] framebuffers at %p / %p (PSRAM), %d bytes each\n",
                fb, bg, SCR_W * SCR_H * 2);

  buildBackground();

  Backlight_Init();
  LCD_Init();
  LCD_pushFull(bg);
  LCD_waitTrans();
  Set_Backlight(85);

  imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS);
  if (imuOk) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_250Hz,
                            SensorQMI8658::LPF_MODE_3);
    imu.enableAccelerometer();
    Serial.println("[imu] QMI8658 ready");
    detectOrientation();
  } else {
    Serial.println("[imu] QMI8658 NOT found - clawd will just sit there");
  }

  Serial.printf("[sprite] %dx%d, %d bytes, body 0x%04X detail 0x%04X\n",
                CLAWD_W, CLAWD_H, CLAWD_W * CLAWD_H / 4,
                CLAWD_BODY_RGB565, CLAWD_DETAIL_RGB565);
  Serial.printf("[btn] GPIO0 reads %d (1 = released, 0 = pressed); exit gesture armed in %lu ms\n",
                digitalRead(0), EXIT_ARM_DELAY_MS);
  Serial.println("[clawd-roll] running. Long-press BOOT to return to the stock system.");

  // A lazy first push, so the screen is alive the moment it appears rather than
  // showing a motionless sprite waiting to be nudged.
  vx =  1.7f;
  vy = -1.2f;

  bootMs = millis();
}

// ================================================================== loop ======
void loop()
{
  const unsigned long now = millis();
  float dt = (now - lastFrameUs) / 1000.0f;      // seconds
  lastFrameUs = now;
  if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

  pollExitButton();

  // ---- input: tilt -> acceleration ------------------------------------------
  // Raw in-plane gravity, straight from the sensor. Level means zero, so holding
  // the board flat lets the ball coast and any tilt drives it -- no calibration
  // step whose snapshot could go stale.
  float a[3];
  readAccel(a);

  float rx = a[inPlane[AXIS_X]] * SIGN_X;
  float ry = a[inPlane[AXIS_Y]] * SIGN_Y;

  // Remove the static offset, but only while the board is near level (see the
  // comment on TRIM_GATE). The gate is tested against the pre-trim reading.
  if (fabsf(rx - trimX) < TRIM_GATE && fabsf(ry - trimY) < TRIM_GATE) {
    trimX += (rx - trimX) * TRIM_ALPHA;
    trimY += (ry - trimY) * TRIM_ALPHA;
  }

  float tx = rx - trimX;
  float ty = ry - trimY;
  if (fabsf(tx) < DEADZONE) tx = 0.0f;
  if (fabsf(ty) < DEADZONE) ty = 0.0f;

  vx += tx * ACC_GAIN;
  vy += ty * ACC_GAIN;
  vx *= FRICTION;
  vy *= FRICTION;

  const float sp = sqrtf(vx * vx + vy * vy);
  if (sp > MAX_SPEED) {
    vx *= MAX_SPEED / sp;
    vy *= MAX_SPEED / sp;
  }

  bx += vx;
  by += vy;

  // ---- arena wall: reflect about the radial normal --------------------------
  {
    const float dx = bx - CX;
    const float dy = by - CY;
    const float d  = sqrtf(dx * dx + dy * dy);
    const float lim = ARENA - BALL_R;

    if (d > lim && d > 0.001f) {
      const float nx = dx / d;
      const float ny = dy / d;
      bx = CX + nx * lim;
      by = CY + ny * lim;

      const float vn = vx * nx + vy * ny;
      if (vn > 0.0f) {
        vx -= (1.0f + RESTITUTION) * vn * nx;
        vy -= (1.0f + RESTITUTION) * vn * ny;
        impactX = CX + nx * ARENA;
        impactY = CY + ny * ARENA;
        impactAge = IMPACT_LIFE;
      }
    }
  }

  // Rolling: angular speed proportional to linear speed, as in the reference game.
  // Not a physical model -- it is a stylised "this thing is rolling" cue.
  roll += sp / BALL_R;
  if (roll > 2.0f * PI) roll -= 2.0f * PI;

  // ---- render ---------------------------------------------------------------
  const float ca = cosf(roll);
  const float sa = sinf(roll);

  // sprite bounding box for this frame, so only the touched rows get rebuilt
  int ny0 = (int)floorf(by - BALL_R); if (ny0 < 0) ny0 = 0;
  int ny1 = (int)ceilf (by + BALL_R); if (ny1 > SCR_H - 1) ny1 = SCR_H - 1;

  int uy0 = (ny0 < prevY0) ? ny0 : prevY0;
  int uy1 = (ny1 > prevY1) ? ny1 : prevY1;
  if (impactAge > 0.0f) {
    int iy0 = (int)(impactY - 52); if (iy0 < 0) iy0 = 0;
    int iy1 = (int)(impactY + 52); if (iy1 > SCR_H - 1) iy1 = SCR_H - 1;
    if (iy0 < uy0) uy0 = iy0;
    if (iy1 > uy1) uy1 = iy1;
  }

  // restore those rows from the backdrop, then draw on top
  for (int y = uy0; y <= uy1; y++) {
    memcpy(fb + (size_t)y * SCR_W, bg + (size_t)y * SCR_W, SCR_W * 2);
  }

  drawClawd(ca, sa);
  drawImpact();
  if (impactAge > 0.0f) {
    impactAge -= dt;
    if (impactAge < 0.0f) impactAge = 0.0f;
  }

  LCD_pushBand(uy0, uy1, fb);
  LCD_waitTrans();

  // ---- diagnostics at ~4 Hz -------------------------------------------------
  if (now - lastDiagMs > 250) {
    lastDiagMs = now;
    Serial.printf("[in] tilt x=%+.3f y=%+.3f g (trim %+.3f/%+.3f) -> v=(%+.2f,%+.2f) "
                  "pos=(%.0f,%.0f) r=%.0f btn=%d\n",
                  tx, ty, trimX, trimY, vx, vy, bx, by,
                  sqrtf((bx - CX) * (bx - CX) + (by - CY) * (by - CY)), digitalRead(0));
  }
}

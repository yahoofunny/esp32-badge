#include "game_photos.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <AnimatedGIF.h>
#include <JPEGDEC.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "input.h"
#include "media.h"

static const char *const PHOTO_EXTS[] = { "jpg", "jpeg", "bmp", "gif" };
static const int PHOTO_EXT_COUNT = 4;

static const uint16_t COL_BG   = PX(RGB565(0x00, 0x00, 0x00));
static const uint16_t COL_DIM  = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_TEXT = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_WARN = PX(RGB565(0xE0, 0x50, 0x50));
static const uint16_t COL_ACCENT = PX(RGB565(0x7A, 0x8B, 0xF0));

// How an image of w by h lands on the panel: centred, scaled to fit, never cropped.
struct Fit { float scale; int offX, offY; };

static Fit fitFor(int w, int h)
{
    Fit f;
    const float s = fminf((float)SCR_W / (float)w, (float)SCR_H / (float)h);
    f.scale = s;
    f.offX = (SCR_W - (int)(w * s)) / 2;
    f.offY = (SCR_H - (int)(h * s)) / 2;
    return f;
}

// A running checksum of what was decoded. It cannot show the picture, but it does say
// whether there was one: zero means a black frame, and two runs of the same file agreeing
// means the decode is deterministic.
static uint32_t pixelSum = 0;

static void blitLine(const uint16_t *px, int rx, int ry, int rw, const Fit &fit)
{
    const int dy0 = fit.offY + (int)(ry * fit.scale);
    const int dy1 = fit.offY + (int)((ry + 1) * fit.scale);
    const int dx0 = fit.offX + (int)(rx * fit.scale);
    const int dx1 = fit.offX + (int)((rx + rw) * fit.scale);
    for (int y = dy0; y < dy1; y++) {
        if ((unsigned)y >= (unsigned)SCR_H) continue;
        uint16_t *out = fb + (size_t)y * SCR_W;
        for (int x = dx0; x < dx1; x++) {
            if ((unsigned)x >= (unsigned)SCR_W) continue;
            const int sx = rx + (int)((x - fit.offX) / fit.scale);
            if (sx < rx || sx >= rx + rw) continue;
            const uint16_t c = px[sx - rx];
            pixelSum += c;
            out[x] = PX(c);
        }
    }
}

// --- BMP ----------------------------------------------------------------------------
// A scanline at a time, straight into the framebuffer: a 4000-pixel-wide BMP then costs
// one row of memory instead of 32 MB.
static bool showBmp(const char *path)
{
    File f = SD_MMC.open(path);
    if (!f) return false;

    uint8_t hdr[54];
    if (f.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        f.close();
        return false;
    }
    const uint32_t dataOff = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8)
                             | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    const int32_t w = (int32_t)((uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8)
                                | ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
    const int32_t hRaw = (int32_t)((uint32_t)hdr[22] | ((uint32_t)hdr[23] << 8)
                                   | ((uint32_t)hdr[24] << 16) | ((uint32_t)hdr[25] << 24));
    const uint16_t bpp = (uint16_t)(hdr[28] | (hdr[29] << 8));
    const uint32_t compression = (uint32_t)hdr[30];
    if (w <= 0 || hRaw == 0 || compression != 0 || (bpp != 24 && bpp != 32)) {
        Serial.printf("[photos] bmp: %dx%d, %u bpp, compression %u -- not supported\n",
                      (int)w, (int)hRaw, (unsigned)bpp, (unsigned)compression);
        f.close();
        return false;
    }

    const bool topDown = hRaw < 0;
    const int h = (int)(topDown ? -hRaw : hRaw);
    const int bpx = bpp / 8;
    const int rowBytes = ((w * bpx + 3) / 4) * 4;
    const Fit fit = fitFor(w, h);
    const int dw = (int)(w * fit.scale), dh = (int)(h * fit.scale);

    uint8_t *row = (uint8_t *)malloc(rowBytes);
    if (!row) {
        f.close();
        return false;
    }

    for (int dy = 0; dy < dh; dy++) {
        const int sy = (int)(dy / fit.scale);
        if (sy >= h) break;
        const int fileRow = topDown ? sy : (h - 1 - sy);
        if (!f.seek(dataOff + (uint32_t)fileRow * rowBytes)) break;
        if (f.read(row, rowBytes) != rowBytes) break;
        const int Y = fit.offY + dy;
        if ((unsigned)Y >= (unsigned)SCR_H) continue;
        uint16_t *out = fb + (size_t)Y * SCR_W;
        for (int dx = 0; dx < dw; dx++) {
            const int sx = (int)(dx / fit.scale);
            if (sx >= w) break;
            const uint8_t *p = row + sx * bpx;
            const int X = fit.offX + dx;
            if ((unsigned)X >= (unsigned)SCR_W) continue;
            const uint16_t c = RGB565(p[2], p[1], p[0]);     // BMP stores blue first
            pixelSum += c;
            out[X] = PX(c);
        }
    }

    free(row);
    f.close();
    Serial.printf("[photos] bmp %dx%d at %.2fx\n", w, h, fit.scale);
    return true;
}

// --- JPEG ---------------------------------------------------------------------------
static JPEGDEC jpeg;
static Fit jpegFit;
static bool jpegDrew = false;

static int jpegDrawCb(JPEGDRAW *d)
{
    jpegDrew = true;
    for (int i = 0; i < d->iHeight; i++) {
        blitLine(d->pPixels + (size_t)i * d->iWidth, d->x, d->y + i, d->iWidth, jpegFit);
    }
    return 1;
}

static bool showJpeg(const char *path)
{
    File f = SD_MMC.open(path);
    if (!f) {
        Serial.println("[photos] jpeg: cannot open");
        return false;
    }
    // The File overload takes the draw callback directly: less machinery than four
    // filesystem callbacks for a filesystem the library's caller already has.
    if (!jpeg.open(f, jpegDrawCb)) {
        Serial.printf("[photos] jpeg: decode setup failed (%d)\n", jpeg.getLastError());
        f.close();
        return false;
    }
    const int w = jpeg.getWidth(), h = jpeg.getHeight();

    // Whole-image powers-of-two downscale first, so a twelve-megapixel photo does not have
    // to go through the scaler in the blit. JPEGDEC's scales are 1/2, 1/4 and 1/8.
    int options = 0, shift = 0;
    if (w > SCR_W * 8 && h > SCR_H * 8) { options = JPEG_SCALE_EIGHTH; shift = 3; }
    else if (w > SCR_W * 4 && h > SCR_H * 4) { options = JPEG_SCALE_QUARTER; shift = 2; }
    else if (w > SCR_W * 2 && h > SCR_H * 2) { options = JPEG_SCALE_HALF; shift = 1; }

    jpegFit = fitFor(w >> shift, h >> shift);
    jpegDrew = false;
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    const int ok = jpeg.decode(0, 0, options);
    jpeg.close();
    f.close();
    f.close();
    Serial.printf("[photos] jpeg %dx%d -> %dx%d (1/%d), decode %s, %s\n", w, h, w >> shift,
                  h >> shift, 1 << shift, ok ? "ok" : "failed",
                  jpegDrew ? "drawn" : "no data");
    return ok && jpegDrew;
}

// --- GIF ----------------------------------------------------------------------------
// The callback is one scanline at a time, and a frame may be smaller than the canvas, so
// the line is placed at the frame's own offset. One more thing the examples have to teach
// that the signatures do not: never read to the last byte of the file, or the seek that
// follows stops working.
static AnimatedGIF gif;
static Fit gifFit;
static bool gifOpen = false;
static bool gifDrew = false;

static void gifDrawCb(GIFDRAW *d)
{
    gifDrew = true;
    // pPixels is declared as bytes but in this mode it is 16-bit pixels, one row of
    // iWidth of them, at the frame's own offset in the canvas.
    blitLine((const uint16_t *)d->pPixels, d->iX, d->iY + d->y, d->iWidth, gifFit);
}

static void *GIFOpenFile(const char *fname, int32_t *pSize)
{
    File *f = new File(SD_MMC.open(fname));
    if (!f || !*f) {
        delete f;
        return nullptr;
    }
    *pSize = (int32_t)f->size();
    return f;
}
static void GIFCloseFile(void *p)
{
    File *f = (File *)p;
    if (f) f->close();
    delete f;
}
static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    File *f = (File *)pFile->fHandle;
    int32_t n = iLen;
    // The library's own warning, from its examples: reading the last byte of the file
    // breaks the seek that follows it.
    if ((pFile->iSize - pFile->iPos) < iLen) n = pFile->iSize - pFile->iPos - 1;
    if (n <= 0) return 0;
    n = (int32_t)f->read(pBuf, n);
    pFile->iPos = (int32_t)f->position();
    return n;
}
static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
    File *f = (File *)pFile->fHandle;
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

static bool openGif(const char *path)
{
    gif.begin(LITTLE_ENDIAN_PIXELS);      // the callback then hands over RGB565
    if (!gif.open(path, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, gifDrawCb)) {
        Serial.printf("[photos] gif: open failed (%d)\n", gif.getLastError());
        return false;
    }
    gifFit = fitFor(gif.getCanvasWidth(), gif.getCanvasHeight());
    gifOpen = true;
    return true;
}

// --- the app ------------------------------------------------------------------------
void GamePhotos::showCurrent()
{
    if (gifOpen) {
        gif.close();
        gifOpen = false;
    }
    failed = false;
    pixelSum = 0;
    frameSec = 0.0f;
    frameMs = 0;
    if (media::count() <= 0) return;

    const char *path = media::pathAt(index);
    const char *dot = strrchr(path, '.');
    const char *ext = dot ? dot + 1 : "";

    bool ok = false;
    if (!strcasecmp(ext, "bmp")) {
        ok = showBmp(path);
    } else if (!strcasecmp(ext, "gif")) {
        if (openGif(path)) {
            gifDrew = false;
            ok = gif.playFrame(false, &frameMs) != 0 && gifDrew;
        }
    } else {
        ok = showJpeg(path);
    }
    failed = !ok;
    Serial.printf("[photos] %d/%d %s: %s, pixel sum %08lX\n", index + 1, media::count(),
                  media::nameAt(index), ok ? "shown" : "FAILED", (unsigned long)pixelSum);
}

void GamePhotos::enter()
{
    failed = false;
    pixelSum = 0;
    if (!media::begin()) {
        failed = true;
        return;
    }
    media::list(media::PHOTOS_DIR, PHOTO_EXTS, PHOTO_EXT_COUNT);
    index = 0;
    if (media::count() > 0) showCurrent();
    setDirty(0, SCR_H - 1);
}

void GamePhotos::leave()
{
    if (gifOpen) {
        gif.close();
        gifOpen = false;
    }
    media::end();
    gfx::clear(COL_BG);
}

void GamePhotos::update(float dt)
{
    if (input::tapped() && media::count() > 0) {
        index = (index + 1) % media::count();
        showCurrent();
    }

    // An animated GIF keeps its file open and is stepped from here; a still one has one
    // frame and playFrame then returns false.
    if (gifOpen) {
        frameSec += dt;
        if (frameSec * 1000.0f >= (float)(frameMs > 0 ? frameMs : 100)) {
            frameSec = 0.0f;
            if (!gif.playFrame(false, &frameMs)) {
                gif.close();
                gifOpen = false;
            }
        }
    }
    setDirty(0, SCR_H - 1);
}

void GamePhotos::render()
{
    if (media::count() <= 0) {
        gfx::clear(COL_BG);
        gfx::textCentered("no pictures", 148, COL_DIM, 2);
        gfx::textCentered(media::mounted() ? "put them in /Photos" : "no card", 192,
                          COL_TEXT, 1);
        gfx::textCentered("on the card, then come back", 210, COL_DIM, 1);
        return;
    }
    gfx::fillRect(0, SCR_H - 22, SCR_W, 22, COL_BG);
    char line[32];
    snprintf(line, sizeof(line), "%.24s", media::nameAt(index));
    gfx::textCentered(line, SCR_H - 17, failed ? COL_WARN : COL_TEXT, 1);
}

void GamePhotos::drawIcon(float cx, float cy, float size) const
{
    // A framed picture: a rectangle, a sun and a hill.
    const int w = (int)(size * 0.86f), h = (int)(size * 0.70f);
    const int x = (int)cx - w / 2, y = (int)cy - h / 2;
    gfx::fillRect(x, y, w, 2, COL_ACCENT);
    gfx::fillRect(x, y + h - 2, w, 2, COL_ACCENT);
    gfx::fillRect(x, y, 2, h, COL_ACCENT);
    gfx::fillRect(x + w - 2, y, 2, h, COL_ACCENT);
    gfx::fillCircle((float)(x + w * 0.26f), (float)(y + h * 0.28f), size * 0.06f, COL_ACCENT);
    for (int i = 0; i < w - 8; i++) {
        const int yy = y + h - 6 - (int)((w - 8 - i) * 0.22f);
        gfx::fillRect(x + 4 + i, yy, 1, y + h - 4 - yy, COL_ACCENT);
    }
}

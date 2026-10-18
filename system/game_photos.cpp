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
#include "photo_blit.h"
#include "photo_gif.h"
#include "clawd_album.h"
#include "esp_heap_caps.h"

static const char *const PHOTO_EXTS[] = { "jpg", "jpeg", "bmp", "gif" };
static const int PHOTO_EXT_COUNT = 4;

static const uint16_t COL_BG   = PX(RGB565(0x00, 0x00, 0x00));
static const uint16_t COL_DIM  = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_TEXT = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_WARN = PX(RGB565(0xE0, 0x50, 0x50));

// The delete bin on the label bar: shared by the hit test and the drawing.
static const int BIN_X = 250, BIN_Y = 337, BIN_R = 16;
static const uint16_t COL_ACCENT = PX(RGB565(0x7A, 0x8B, 0xF0));

// Round badge: fill by default; hold a picture to toggle a complete-image view.
using photo::Fit;
static bool fillScreen = true;
static Fit fitFor(int w, int h) { return photo::fitFor(w, h, fillScreen); }

// A running checksum of what was decoded. It cannot show the picture, but it does say
// whether there was one: zero means a black frame, and two runs of the same file agreeing
// means the decode is deterministic.
static uint32_t pixelSum = 0;

static void blitLine(const uint16_t *px, int rx, int ry, int rw, const Fit &fit)
{
    photo::blitLine(fb, px, rx, ry, rw, fit, &pixelSum);
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
static JPEGDEC *jpeg = nullptr;
static Fit jpegFit;
static bool jpegDrew = false;

static int jpegDrawCb(JPEGDRAW *d)
{
    jpegDrew = true;
    for (int i = 0; i < d->iHeight; i++) {
        blitLine(d->pPixels + (size_t)i * d->iWidth, d->x, d->y + i,
                 d->iWidthUsed, jpegFit);
    }
    return 1;
}

static bool showJpeg(const char *path)
{
    if (!jpeg) jpeg = new JPEGDEC();
    File f = SD_MMC.open(path);
    if (!f) {
        Serial.println("[photos] jpeg: cannot open");
        return false;
    }
    // The File overload takes the draw callback directly: less machinery than four
    // filesystem callbacks for a filesystem the library's caller already has.
    if (!jpeg->open(f, jpegDrawCb)) {
        Serial.printf("[photos] jpeg: decode setup failed (%d)\n", jpeg->getLastError());
        f.close();
        return false;
    }
    const int w = jpeg->getWidth(), h = jpeg->getHeight();

    // Whole-image powers-of-two downscale first, so a twelve-megapixel photo does not have
    // to go through the scaler in the blit. JPEGDEC's scales are 1/2, 1/4 and 1/8.
    int options = 0, shift = 0;
    if (w > SCR_W * 8 && h > SCR_H * 8) { options = JPEG_SCALE_EIGHTH; shift = 3; }
    else if (w > SCR_W * 4 && h > SCR_H * 4) { options = JPEG_SCALE_QUARTER; shift = 2; }
    else if (w > SCR_W * 2 && h > SCR_H * 2) { options = JPEG_SCALE_HALF; shift = 1; }

    const int round = (1 << shift) - 1;
    jpegFit = fitFor((w + round) >> shift, (h + round) >> shift);
    jpegDrew = false;
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
    const int ok = jpeg->decode(0, 0, options);
    jpeg->close();
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
static AnimatedGIF *gif = nullptr;
static Fit gifFit;
static bool gifOpen = false;
static bool gifDrew = false;
static bool gifLoopPending=false;
static uint16_t *gifPixels=nullptr,*gifRestore=nullptr;
static photo::GifCanvas gifCanvas;
static photo::GifView gifView;
static uint32_t perfStart=0,perfFrames=0,perfDecodeUs=0,perfMaxUs=0,perfBlitUs=0;
static uint32_t perfScaleUs=0;
static uint32_t perfBlitStart=0;

static int playGifFrame(int *delayMs) {
    const uint32_t started=micros();
    const int result=gif->playFrame(false,delayMs);
    const uint32_t elapsed=micros()-started;
    if(gifDrew){
        ++perfFrames;perfDecodeUs+=elapsed;if(elapsed>perfMaxUs)perfMaxUs=elapsed;
        const uint32_t scaleStart=micros();gifView.draw(fb,gifPixels);
        perfScaleUs+=micros()-scaleStart;
        if(perfFrames==1)for(int i=0;i<SCR_W*SCR_H;++i)pixelSum+=fb[i];
    }
    return result;
}

static void *gifAlloc(uint32_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void closeGif()
{
    if (!gifOpen) return;
    gif->close();
    free(gifPixels);free(gifRestore);gifPixels=gifRestore=nullptr;
    gifOpen = false;
}

static void gifDrawCb(GIFDRAW *d)
{
    perfBlitStart=micros();
    gifDrew = true;
    if(d->y==0)gifCanvas.frame(d->iX,d->iY,d->iWidth,d->iHeight,d->ucDisposalMethod);
    gifCanvas.line(d->iX,d->iY+d->y,d->iWidth,d->pPixels,d->pPalette,
                   d->ucHasTransparency,d->ucTransparent);
    perfBlitUs+=micros()-perfBlitStart;
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

static bool openGif(const char *path,const clawd_album::Entry *builtIn=nullptr)
{
    if (!gif) gif = new AnimatedGIF();
    gif->begin(LITTLE_ENDIAN_PIXELS);      // the callback then hands over RGB565
    // AnimatedGIF's flash API is read-only despite its historical uint8_t* argument.
    const bool opened=builtIn
        ?gif->openFLASH(const_cast<uint8_t*>(builtIn->data),builtIn->size,gifDrawCb)
        :gif->open(path,GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,gifDrawCb);
    if (!opened) {
        Serial.printf("[photos] gif: open failed (%d)\n", gif->getLastError());
        return false;
    }
    gifFit = fitFor(gif->getCanvasWidth(), gif->getCanvasHeight());
    gifOpen = true;
    const int w = gif->getCanvasWidth(), h = gif->getCanvasHeight();
    if (w <= 0 || h <= 0 || uint64_t(w)*h*4>4*1024*1024 ||
        !(gifPixels=(uint16_t*)gifAlloc(w*h*2)) ||
        !(gifRestore=(uint16_t*)gifAlloc(w*h*2))) {
        Serial.println("[photos] gif: canvas too large or no PSRAM");
        closeGif();
        return false;
    }
    gifCanvas.begin(w,h,gifPixels,gifRestore);
    if(builtIn&&fillScreen) {
        // Consistent body scale, centered visible composition, including every
        // frame's accessories. Empty transparent source margins are not art.
        gifView.beginContent(w,h,builtIn->contentX,builtIn->contentY,
                            builtIn->contentW,builtIn->contentH);
    } else gifView.begin(w,h,gifFit.width,gifFit.height,gifFit.offX,gifFit.offY);
    gifLoopPending=false;
    gif->setDrawType(GIF_DRAW_RAW);
    return true;
}

// --- the app ------------------------------------------------------------------------
// --- the frame cache ----------------------------------------------------------------
// Switching pictures used to re-decode, which is 100-300 ms of nothing for a camera photo.
// A decoded 360x360 frame is 259 KB of PSRAM and there are 8 MB of it, so three frames --
// previous, current, next -- turn a step in either direction into a copy. The label strip
// is drawn after the copy, which is why a cached frame is the picture alone.
//
// Animated GIFs are not cached: their frames come from a decoder that keeps the file open,
// and the animation is the picture. A still is what benefits.
static const int SLOT_PREV = 0, SLOT_CUR = 1, SLOT_NEXT = 2;
static const int CACHE_SLOTS = 3;
static const uint32_t FRAME_BYTES = (uint32_t)SCR_W * SCR_H * 2;
static uint16_t *frameCache[CACHE_SLOTS] = { nullptr, nullptr, nullptr };
static int  cacheIndex[CACHE_SLOTS] = { -1, -1, -1 };

// Bumped by every step. The pre-decode draws into the framebuffer; if the user steps while
// it is running, putting "the visible picture" back would overwrite the picture they just
// asked for, and storing the frame it decoded would file it under the wrong name. The
// generation says whether anything changed underneath it.
static uint32_t stepGen = 0;

static uint16_t *cacheSlot(int slot)
{
    if (!frameCache[slot]) {
        frameCache[slot] = (uint16_t *)heap_caps_malloc(
            FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return frameCache[slot];
}

static void cacheForget()
{
    for (int i = 0; i < CACHE_SLOTS; i++) cacheIndex[i] = -1;
}

// Is this entry a still picture, i.e. something worth caching? The built-in album entries
// are animated GIFs, and their names carry no extension.
static bool cacheStill(int index)
{
    const auto *builtIn = clawd_album::at(index);
    const char *path = builtIn ? builtIn->name
                               : media::pathAt(index - clawd_album::count());
    const char *dot = strrchr(path, '.');
    return dot && strcasecmp(dot + 1, "gif") != 0;
}

// Copies a cached frame out, if some slot holds this picture.
static bool cachePresent(int index)
{
    for (int i = 0; i < CACHE_SLOTS; i++) {
        if (cacheIndex[i] != index) continue;
        if (!frameCache[i]) continue;
        memcpy(fb, frameCache[i], FRAME_BYTES);
        return true;
    }
    return false;
}

// One entry decoded into the framebuffer. Both the visible path and the pre-decode below
// go through here, so there is one place that knows the formats.
bool GamePhotos::decodeEntry(int index)
{
    const auto *builtIn = clawd_album::at(index);
    const char *path = builtIn ? builtIn->name
                               : media::pathAt(index - clawd_album::count());
    const char *dot = strrchr(path, '.');
    const char *ext = dot ? dot + 1 : "";

    if (!strcasecmp(ext, "bmp")) return showBmp(path);
    if (!strcasecmp(ext, "gif")) {
        if (!openGif(path, builtIn)) return false;
        gifDrew = false;
        gifLoopPending = playGifFrame(&frameMs) == 0;
        frameDueMs = millis() + (frameMs > 0 ? frameMs : 100);
        return gifDrew;
    }
    return showJpeg(path);
}

int GamePhotos::pictureCount() const {return clawd_album::count()+media::count();}
const char *GamePhotos::currentName() const {
    const auto *entry=clawd_album::at(index);
    return entry?entry->name:media::nameAt(index-clawd_album::count());
}
// A step rotates what each slot means. The buffers rotate with the labels: relabelling
// alone left every slot describing another slot's pixels, which is a cache that hands back
// the wrong picture.
static void rotateForward()
{
    uint16_t *freed = frameCache[SLOT_PREV];
    frameCache[SLOT_PREV] = frameCache[SLOT_CUR];
    frameCache[SLOT_CUR] = frameCache[SLOT_NEXT];
    frameCache[SLOT_NEXT] = freed;
    cacheIndex[SLOT_PREV] = cacheIndex[SLOT_CUR];
    cacheIndex[SLOT_CUR] = cacheIndex[SLOT_NEXT];
    cacheIndex[SLOT_NEXT] = -1;
}

static void rotateBackward()
{
    uint16_t *freed = frameCache[SLOT_NEXT];
    frameCache[SLOT_NEXT] = frameCache[SLOT_CUR];
    frameCache[SLOT_CUR] = frameCache[SLOT_PREV];
    frameCache[SLOT_PREV] = freed;
    cacheIndex[SLOT_NEXT] = cacheIndex[SLOT_CUR];
    cacheIndex[SLOT_CUR] = cacheIndex[SLOT_PREV];
    cacheIndex[SLOT_PREV] = -1;
}

void GamePhotos::next() {
    if(pictureCount()<=0)return;
    stepGen++;
    rotateForward();
    index=(index+1)%pictureCount();showCurrent();
}
void GamePhotos::previous() {
    if(pictureCount()<=0)return;
    stepGen++;
    rotateBackward();
    index=(index+pictureCount()-1)%pictureCount();showCurrent();
}
void GamePhotos::showCurrent()
{
    closeGif();
    gfx::clear(COL_BG);
    failed = false;
    pixelSum = 0;
    frameDueMs = 0;
    frameMs = 0;
    perfStart=millis();perfFrames=perfDecodeUs=perfMaxUs=perfBlitUs=perfScaleUs=0;
    needsPresent=true;
    if (pictureCount() <= 0) return;

    const uint32_t started = millis();
    char line[80];

    // A frame we already hold: copy it and be done. This is the whole point of the cache.
    if (cachePresent(index)) {
        // The cache holds panel-order pixels; accumulate the same host-order sum the
        // decode path reports, so the two logs can be compared.
        pixelSum = 0;
        for (int i = 0; i < SCR_W * SCR_H; i++) pixelSum += BSWAP16(fb[i]);
        snprintf(line, sizeof(line), "[photos] %d/%d %s: from cache in %u ms, sum %08lX",
                 index + 1, pictureCount(), currentName(), (unsigned)(millis() - started),
                 (unsigned long)pixelSum);
        Serial.println(line);
        return;
    }

    const bool ok = decodeEntry(index);
    if (ok && cacheStill(index)) {
        uint16_t *slot = cacheSlot(SLOT_CUR);
        if (slot) { memcpy(slot, fb, FRAME_BYTES); cacheIndex[SLOT_CUR] = index; }
    }
    failed = !ok;
    snprintf(line, sizeof(line), "[photos] %d/%d %s: %s in %u ms, sum %08lX", index + 1,
             pictureCount(), currentName(), ok ? "shown" : "FAILED",
             (unsigned)(millis() - started), (unsigned long)pixelSum);
    Serial.println(line);
}

void GamePhotos::deleteCurrent()
{
    if (index < clawd_album::count()) {
        Serial.println("[photos] built-in picture, not deletable");
        return;
    }
    const char *path = media::pathAt(index - clawd_album::count());
    if (!*path) return;
    if (!SD_MMC.remove(path)) {
        Serial.printf("[photos] could not remove %s", path); Serial.println();
        failed = true;
        return;
    }
    Serial.printf("[photos] removed %s", path); Serial.println();
    closeGif();
    cacheForget();                       // the cache describes a library that changed
    media::list(media::PHOTOS_DIR, PHOTO_EXTS, PHOTO_EXT_COUNT);
    if (pictureCount() <= 0) {
        index = 0;
        failed = false;
        needsPresent = true;
        return;
    }
    if (index >= pictureCount()) index = pictureCount() - 1;
    showCurrent();
}

void GamePhotos::enter()
{
    gesture.reset();
    failed = false;
    pixelSum = 0;
    media::begin(); // Built-in photos remain available with no SD card.
    media::list(media::PHOTOS_DIR, PHOTO_EXTS, PHOTO_EXT_COUNT);
    Serial.printf("[photos] album: %d built-in + %d SD pictures\n",clawd_album::count(),media::count());
    index = 0;
    if (pictureCount() > 0) showCurrent();
    setDirty(0, SCR_H - 1);
}

void GamePhotos::leave()
{
    closeGif();
    media::end();
    gfx::clear(COL_BG);
}

void GamePhotos::update(float dt)
{
    setDirty(1,0);
    // The bin sits at the right end of the label bar. Two taps: the first arms it, the
    // second removes the file. Built-in pictures live in the firmware, so for those the
    // bin does nothing at all.
    const int tx = input::tapX(), ty = input::tapY();
    const bool onBin = (tx - BIN_X) * (tx - BIN_X) + (ty - BIN_Y) * (ty - BIN_Y) <= BIN_R * BIN_R;
    bool consumed = false;
    if (input::tapped()) {
        if (onBin) {
            consumed = true;
            if (!deleteArmed) { deleteArmed = true; needsPresent = true; }
            else { deleteArmed = false; deleteCurrent(); }
        } else if (deleteArmed) {
            deleteArmed = false;
            needsPresent = true;
        }
    }

    if (pictureCount() > 0) {
        const auto action=gesture.update(input::touchDown(),input::tapped() && !consumed,
            input::tapX(),input::tapY(),input::touchHeldMs());
        if (action.toggleFit) {
            fillScreen = !fillScreen;
            cacheForget();
            Serial.printf("[photos] view: %s\n", fillScreen ? "fill" : "fit");
            showCurrent();
        }
        if(action.step>0)next();
        else if(action.step<0)previous();
    }

    // An animated GIF keeps its file open and is stepped from here; a still one has one
    // frame and playFrame then returns false.
    if (gifOpen) {
        const uint32_t now=millis();
        if (int32_t(now-frameDueMs)>=0) {
            if(gifLoopPending){gif->reset();gifCanvas.reset();gifLoopPending=false;}
            gifDrew=false;
            const int result=playGifFrame(&frameMs);
            gifLoopPending=result==0;
            if(gifDrew)needsPresent=true;
            const uint32_t delayMs=frameMs>0?frameMs:100;
            frameDueMs+=delayMs;
            if(int32_t(millis()-frameDueMs)>int32_t(delayMs*2))frameDueMs=millis()+delayMs;
        }
    }
    // With a still on screen and nothing else happening, decode the next one into the
    // cache: draw it into the framebuffer, copy it out, put the visible picture back. The
    // screen never changes, so the cost is a moment of CPU spent while the user is looking
    // at the current picture -- exactly the moment a switch would otherwise stall for half
    // a second on a twelve-megapixel photo.
    if (!gifOpen && pictureCount() > 1 && !needsPresent) {
        static uint32_t lastTry = 0;
        if (millis() - lastTry > 500) {
            lastTry = millis();
            const int n = (index + 1) % pictureCount();
            if (cacheIndex[SLOT_NEXT] != n && cacheStill(n)
                && cacheIndex[SLOT_CUR] == index && frameCache[SLOT_CUR]) {
                const uint32_t t0 = millis();
                const uint32_t gen = stepGen;
                const bool ok = decodeEntry(n);
                if (stepGen != gen) {
                    // Stepped mid-decode: what is in the framebuffer is not the picture
                    // that is showing. Redraw the right one rather than papering over it.
                    Serial.println("[photos] pre-decode abandoned: stepped during it");
                    showCurrent();
                    return;
                }
                if (ok) {
                    uint16_t *slot = cacheSlot(SLOT_NEXT);
                    if (slot) { memcpy(slot, fb, FRAME_BYTES); cacheIndex[SLOT_NEXT] = n; }
                }
                memcpy(fb, frameCache[SLOT_CUR], FRAME_BYTES);
                needsPresent = true;
                char line[64];
                snprintf(line, sizeof(line), "[photos] predecoded %d/%d in %u ms", n + 1,
                         pictureCount(), (unsigned)(millis() - t0));
                Serial.println(line);
            }
        }
    }
    if(needsPresent)setDirty(0,SCR_H-1);
}

void GamePhotos::printDiagnostics() const {
    const uint32_t elapsed=millis()-perfStart;
    Serial.printf("[photos-perf] file=%s frames=%u elapsed_ms=%u fps=%.2f decode_avg_us=%u decode_max_us=%u compose_avg_us=%u scale_avg_us=%u delay_ms=%d\n",
        currentName(),perfFrames,elapsed,elapsed?perfFrames*1000.0f/elapsed:0,
        perfFrames?perfDecodeUs/perfFrames:0,perfMaxUs,perfFrames?perfBlitUs/perfFrames:0,
        perfFrames?perfScaleUs/perfFrames:0,frameMs);
}
void GamePhotos::captureFrame() const {
    Serial.printf("[photos-frame] %d %d %u\n",SCR_W,SCR_H,unsigned(SCR_W*SCR_H*2));
    Serial.write((const uint8_t*)fb,SCR_W*SCR_H*2);
    Serial.println("\n[photos-frame-end]");
}

void GamePhotos::render()
{
    if(!needsPresent)return;
    needsPresent=false;
    if (pictureCount() <= 0) {
        gfx::clear(COL_BG);
        gfx::textCentered("no pictures", 148, COL_DIM, 2);
        gfx::textCentered(media::mounted() ? "put them in /Photos" : "no card", 192,
                          COL_TEXT, 1);
        gfx::textCentered("on the card, then come back", 210, COL_DIM, 1);
        return;
    }
    if (failed) {
        gfx::clear(COL_BG);
        gfx::textCentered("cannot show image", 140, COL_WARN, 1);
        gfx::textCentered("try a baseline JPG or BMP", 165, COL_TEXT, 1);
        gfx::textCentered("tap for next picture", 190, COL_DIM, 1);
        char line[32];
        snprintf(line, sizeof(line), "%.24s", currentName());
        gfx::textCentered(line, 215, COL_DIM, 1);
    }
    // Always redraw after GIF pixels; this stays inside the round panel's chord.
    const uint16_t panel=PX(RGB565(0x12,0x12,0x20));
    gfx::fillCircle(145,337,12,panel);gfx::fillCircle(215,337,12,panel);
    gfx::fillRect(145,325,70,24,panel);
    char count[24];snprintf(count,sizeof count,"%d / %d",index+1,pictureCount());
    gfx::textCentered(count,333,COL_TEXT,1);

    // The bin: dim by default, red and ringed once armed, so the second tap is obviously a
    // confirmation rather than another delete.
    {
        const uint16_t c = deleteArmed ? COL_WARN : COL_DIM;
        const int lid = deleteArmed ? 10 : 8;
        gfx::fillRect(BIN_X - lid, BIN_Y - 9, 2 * lid, 2, c);          // lid
        gfx::fillRect(BIN_X - lid + 3, BIN_Y - 12, 2 * lid - 6, 2, c); // handle
        gfx::fillRect(BIN_X - lid + 1, BIN_Y - 6, 2 * lid - 2, 12, c); // body
        if (deleteArmed) gfx::strokeCircle((float)BIN_X, (float)BIN_Y, 16.0f, 2.0f, COL_WARN);
    }
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

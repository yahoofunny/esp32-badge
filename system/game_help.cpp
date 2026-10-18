#include "game_help.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "assets_cjk.h"
#include "assets_font.h"
#include "gfx.h"
#include "input.h"

// One idea per page, a few characters each. The person this is handed to should be able to
// finish it without being told anything else, so it also explains the gestures the
// individual apps use -- the album's long press, the pet's menu, how the assistant is
// entered -- and ends on the only thing that matters.
static const char *const LINES[][4] = {
    { "你好！我是 clawd", "这是你的吧唧", nullptr },
    { "拖动圆环", "换到下一个", nullptr },
    { "点一下图标", "就能打开", nullptr },
    { "按住上面的按键", "回到主页", nullptr },
    { "在主页按住按键", "小智就来了", nullptr },
    { "相册", "点一下 换一张", "长按 切换铺满", nullptr },
    { "宠物", "点它 它会开心", "长按 可以拖走", nullptr },
    { "宠物下面的按钮", "喂食 玩耍 摸摸", "洗澡 睡觉 改名", nullptr },
    { "音乐", "点中间 播放暂停", "点两边 换歌", nullptr },
    { "上面滑条", "拖动调音量", nullptr },
    { "传照片和音乐", "设置里 打开传文件", "按屏幕上的地址上传", nullptr },
    { "没电了", "用 USB-C 充电", nullptr },
    { "祝你玩得开心", nullptr },
};
static const int PAGE_COUNT = (int)(sizeof(LINES) / sizeof(LINES[0]));

static const int TITLE_Y = 44;
static const int LINE_PITCH = 44;     // 32 px glyphs plus a gap
static const int DOTS_Y = 292;
static const int HINT_Y = 316;

static const uint16_t COL_BG     = PX(RGB565(0x0C, 0x12, 0x0E));
static const uint16_t COL_TEXT   = PX(RGB565(0xF2, 0xF2, 0xF6));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x7E, 0x72));
static const uint16_t COL_ACCENT = PX(RGB565(0x5F, 0xC4, 0x7A));

// Everything here is drawn at one of two scales: Chinese glyphs are 16x16 doubled, Latin
// text is the system 8x8 font at four times, which puts both on a 32-pixel line.
static const int CJK_SCALE = 2;
static const int ASCII_SCALE = 4;

static const uint16_t *glyphFor(uint16_t cp)
{
    int lo = 0, hi = CJK_COUNT - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (cjk_codepoints[mid] == cp) return cjk_bits[mid];
        if (cjk_codepoints[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

// Decodes one UTF-8 sequence. Returns the codepoint and moves the pointer.
static uint16_t nextCodepoint(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint16_t cp;
    if (p[0] < 0x80) { cp = p[0]; *s += 1; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = (uint16_t)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F)); *s += 2; }
    else if ((p[0] & 0xF0) == 0xE0) {
        cp = (uint16_t)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        *s += 3;
    } else { cp = '?'; *s += 1; }
    return cp;
}

static int glyphAdvance(uint16_t cp)
{
    return (cp < 0x80) ? (7 * ASCII_SCALE) : (CJK_GLYPH * CJK_SCALE);
}

static int lineWidth(const char *s)
{
    int w = 0;
    while (*s) w += glyphAdvance(nextCodepoint(&s));
    return w;
}

static void drawUtf8(const char *s, int y, uint16_t colour)
{
    int x = (int)(SCR_CX - lineWidth(s) * 0.5f);
    while (*s) {
        const uint16_t cp = nextCodepoint(&s);
        if (cp < 0x80) {
            char one[2] = { (char)cp, 0 };
            gfx::text(one, x, y, colour, ASCII_SCALE);
        } else if (const uint16_t *bits = glyphFor(cp)) {
            for (int row = 0; row < CJK_GLYPH; row++) {
                for (int col = 0; col < CJK_GLYPH; col++) {
                    if (bits[row] & (1 << (15 - col))) {
                        gfx::fillRect(x + col * CJK_SCALE, y + row * CJK_SCALE,
                                      CJK_SCALE, CJK_SCALE, colour);
                    }
                }
            }
        }
        x += glyphAdvance(cp);
    }
}

void GameHelp::enter()
{
    page = 0;
    setDirty(0, SCR_H - 1);
}

void GameHelp::leave()
{
    gfx::clear(COL_BG);
}

void GameHelp::update(float dt)
{
    (void)dt;
    // Static pages: a frame is drawn only when the page changes, and nothing is sent
    // otherwise. Reading a tutorial should not cost battery.
    setDirty(1, 0);
    if (!input::tapped()) return;
    page = (page + 1) % PAGE_COUNT;
    setDirty(0, SCR_H - 1);
}

void GameHelp::render()
{
    if (dirtyY1 < dirtyY0) return;      // nothing changed since the last frame

    gfx::clear(COL_BG);
    drawUtf8("怎么玩", TITLE_Y, COL_ACCENT);

    const char *const *lines = LINES[page];
    int count = 0;
    while (count < 3 && lines[count]) count++;
    const int top = 150 - (count - 1) * LINE_PITCH / 2;
    for (int i = 0; i < count; i++) {
        drawUtf8(lines[i], top + i * LINE_PITCH, COL_TEXT);
    }

    // Page dots, so a child can see there is more.
    const int span = PAGE_COUNT * 12;
    for (int i = 0; i < PAGE_COUNT; i++) {
        const int x = (int)(SCR_CX - span * 0.5f + i * 12 + 5);
        gfx::fillCircle((float)x, (float)DOTS_Y, i == page ? 3.5f : 2.0f,
                        i == page ? COL_ACCENT : COL_DIM);
    }

    drawUtf8("点一下 下一页", HINT_Y, COL_DIM);
}

void GameHelp::drawIcon(float cx, float cy, float size) const
{
    // A question mark: the one glyph that says "read this" in any language.
    const int top = (int)(cy - size * 0.42f);
    const int w = (int)(size * 0.09f);
    // the hook
    for (int a = 0; a <= 180; a += 10) {
        const float rad = (float)a * PI_F / 180.0f;
        const int x = (int)(cx + cosf(rad + PI_F) * size * 0.16f);
        const int y = (int)(top + size * 0.16f - sinf(rad) * size * 0.16f);
        gfx::fillRect(x, y, w, w, COL_ACCENT);
    }
    gfx::fillRect((int)(cx - w / 2), top + (int)(size * 0.30f), w, (int)(size * 0.18f),
                  COL_ACCENT);
    gfx::fillRect((int)(cx - w / 2), top + (int)(size * 0.58f), w, w, COL_ACCENT);
}

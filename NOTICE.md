# Third-party notices

This repository is licensed AGPL-3.0 (see [LICENSE](LICENSE)). It also contains or
derives from work by others, listed here with its own terms.

## XiaoZhi voice client — MIT

The optional voice-pet build uses [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
v2.3.0 (MIT), copyright 2025 Shenzhen Xinzhi Future Technology Co., Ltd. and contributors.
The preparation script checks out that source with its original LICENSE intact;
custom display/platform integration is in `firmware/clawd_pet/`. Its ESP-IDF and
managed dependencies retain their upstream notices and licenses.

## The clawd artwork — AGPL-3.0

The creature used by the rolling game, the pet and the project icon comes from
[clawd-on-desk](https://github.com/rullerzhou-afk/clawd-on-desk), an AGPL-3.0 desktop
pet, by way of the blog it was first copied into.

| File | What it is |
|---|---|
| `games/clawd-roll/clawd-source.png` | the source art |
| `games/clawd-roll/clawd_asset.h` | 2bpp indexed sprite generated from it |
| `system/clawd-source.png` | the same source art, kept beside the firmware |
| `system/assets_pet.h` | the same art at four sizes, the pet's body |
| `games/clawd-roll/icon.png`, `icon-256.png` | the project icon, composed from it |
| `system/assets_icons.h` | the same art, posed three ways |
| `assets/clawd-gifs/*.gif` | 24 original clawd-on-desk animations used by PHOTOS and as wardrobe references |
| `system/assets_clawd_album.h` | unchanged GIF bytes embedded in flash by `tools/make_clawd_album.py` |
| `system/outfits.cpp` | small pixel accessories drawn over the existing pet body, inspired by those references |

The launcher icons are not new artwork: `tools/make_icon_sprites.py` takes the source
above and moves a few pixel runs in it -- leaning it, raising one arm, adding ears -- so
the three entries read as three poses of one creature. The pet wardrobe retains 24
animations from clawd-on-desk's `assets/gif` under `assets/clawd-gifs/` as design
references and as complete animated pictures in PHOTOS. The album embeds the original
GIF bytes unchanged; it does not redraw the artwork. Separately, the pet draws small
accessories with pixel primitives in `system/outfits.cpp`, preserving its existing
body dimensions.

This choice of licence is what made AGPL-3.0 the right one for the repository as a
whole: keeping the art and shipping the source under anything more permissive would
not have been consistent.

Worth stating plainly: the *character* is Anthropic's, the mascot for Claude Code.
clawd-on-desk is fan work around it and could not have licensed the character either.
This repository inherits that ambiguity rather than resolving it. If the artwork is
ever replaced with something original, the AGPL obligation on it goes away with the
files — `tools/convert_sprite.py` regenerates the sprite from any similar flat-colour
image, so that swap is cheap.

## Board drivers — Apache-2.0

`lib/board_drivers/` is derived from Waveshare's
[ESP32-S3-Touch-LCD-1.85B repository](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B),
which is Apache-2.0. Modifications are documented in the files themselves; the two
that matter are the non-mutating push functions in `Display_ST77916.{h,cpp}` and the
auto-sleep fix in `Touch_CST816.{h,cpp}`.

Apache-2.0 is compatible with AGPL-3.0, so the combination is fine.

## SD music decoder — GPL-3.0

`lib/badge_audio_i2s/` contains a local snapshot of
[schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S), whose
source identifies version 3.0.8s (March 23, 2024). Its GPL-3.0 license and decoder
source notices are retained. Badge-specific changes are documented in
`lib/badge_audio_i2s/BADGE-NOTES.md`: unique include/source names, 256fs MCLK for
the ES8311 and disabling the IDF 5 I2S channel before deletion.
The optional `Badge-Loop.wav` demo is synthesized by this project's own code;
no third-party recordings or songs are bundled.

## The pet's design — MIT

The pet follows the model of
[claude-code-tamagotchi](https://github.com/Ido-Levi/claude-code-tamagotchi) (MIT):
four needs that all read "fuller is better", a derived health value, sickness that
starts and ends on its own, and growth gated on accumulated care. That project's
*design* was adopted; no code was copied from it, and its kaomoji artwork is not used.

The pet's body is clawd instead, from the artwork listed above. `tools/make_pet_sprites.py`
reduces it to one sprite per life stage, which is what makes growing up visible, and
locates the eyes in the source so the firmware can draw its own over the top: the eyes
are left out of the sprites and redrawn each frame, so a single body per stage carries
every mood rather than needing a bitmap per emotion.

## The bitmap font

`system/assets_font.h` was rasterised from a system font by `tools/make_font.py`.
Individual glyphs are 8x8 pixels and carry no meaningful design copyright, but the
input face was not an open one. Regenerating from DejaVu Sans Mono or another free
face is a one-line change to the generator if the provenance is unwanted:
`python tools/make_font.py system/assets_font.h --font <path-to-ttf>`.

## Hardware documentation

Pin assignments, the factory partition layout and the two-firmware arrangement were
read back from the device and from Waveshare's published documentation and
repository rather than copied from any source project.

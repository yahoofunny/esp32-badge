#!/usr/bin/env python3
"""Prepare a pinned XiaoZhi source checkout with the badge's own pet display.

Does not flash. Run tools/idf_env.bat idf.py in reference/xiaozhi afterwards.
All modified upstream files are recreated from the pinned commit on each run.
"""
import argparse
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_COMMIT = "1b48ebd7863695bf80d384c6c09af6299a6d7d0e"  # v2.3.0, IDF >=5.5.2

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, default=ROOT / "reference/xiaozhi")
    args = ap.parse_args()
    dest = args.source.resolve()
    if not dest.exists():
        subprocess.run(["git", "clone", "--branch", "v2.3.0", "--single-branch",
                        "https://github.com/78/xiaozhi-esp32.git", str(dest)], check=True)
    head = subprocess.check_output(["git", "-C", str(dest), "rev-parse", "HEAD"], text=True).strip()
    if head != UPSTREAM_COMMIT:
        raise SystemExit(f"Expected XiaoZhi v2.3.0 ({UPSTREAM_COMMIT}), found {head}; use a dedicated checkout")

    def original(path):
        return subprocess.check_output(["git", "-C", str(dest), "show", f"{UPSTREAM_COMMIT}:{path}"]).decode()
    def write(path, value):
        p = dest / path
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text(encoding="utf-8") != value:
            p.write_text(value, encoding="utf-8")

    overlay = ROOT / "firmware/clawd_pet"
    if (overlay / "dependencies.lock").exists():
        shutil.copy2(overlay / "dependencies.lock", dest / "dependencies.lock")
    target = dest / "main/clawd_pet"
    shutil.copytree(overlay / "compat", target / "compat", dirs_exist_ok=True)
    for name in ("pet_display.h", "pet_display.cc", "pet_platform.h", "pet_platform.cc", "pet_ui_pacing.h"):
        shutil.copy2(overlay / name, target / name)
    for name in ("game.h", "game_pet.h", "game_pet.cpp", "gfx.h", "gfx.cpp", "input.h",
                 "clock.h", "sound.h", "assets_font.h", "assets_mon.h",
                 "outfits.h", "outfits.cpp"):
        shutil.copy2(ROOT / "system" / name, target / name)

    cmake = original("main/CMakeLists.txt")
    marker = "idf_component_register(SRCS ${SOURCES}"
    extra = '''list(APPEND SOURCES "clawd_pet/game_pet.cpp" "clawd_pet/gfx.cpp" "clawd_pet/outfits.cpp"
    "clawd_pet/pet_display.cc" "clawd_pet/pet_platform.cc")
list(APPEND INCLUDE_DIRS "clawd_pet/compat")
'''
    assert marker in cmake
    cmake = cmake.replace(marker, extra + marker)
    cmake += '\ntarget_compile_definitions(${COMPONENT_LIB} PRIVATE CLAWD_NATIVE_RGB565=1)\n'
    # Keep the official board identity for cloud provisioning; disable firmware OTA
    # below so a stock upgrade cannot overwrite this display or the launcher slot.
    write("main/CMakeLists.txt", cmake)
    # v2.3.0 ships the 1.85B driver and CMake branch but omits its board-choice
    # declaration. Without this it silently selects the default breadboard target.
    kconfig = original("main/Kconfig.projbuild")
    marker = "    config BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85C"
    assert marker in kconfig
    kconfig = kconfig.replace(marker,
        '    config BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B\n'
        '        bool "Waveshare ESP32-S3-Touch-LCD-1.85B (CLAWD PET)"\n'
        '        depends on IDF_TARGET_ESP32S3\n\n' + marker)
    write("main/Kconfig.projbuild", kconfig)
    boardpath = "main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc"
    board = original(boardpath).replace('#include "display/lcd_display.h"', '#include "clawd_pet/pet_display.h"')
    board = board.replace("new SpiLcdDisplay(panel_io, panel,", "new ClawdPetDisplay(i2c_bus_, panel_io, panel,")
    marker = "DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);"
    assert board.count(marker) == 1
    board = board.replace(marker, marker + '\n        static_cast<ClawdPetDisplay*>(display_)->SetInteractionCallback([this]() { power_save_timer_->WakeUp(); });')
    board = board.replace("        InitializeButtons();", "        InitializeButtons();\n        boot_button_.OnLongPress([]() { clawd::ReturnToLauncher(); });")
    write(boardpath, board)
    ota = original("main/ota.cc")
    marker = "    cJSON_Delete(root);\n    return ESP_OK;"
    assert marker in ota, "Upstream OTA parser changed"
    ota = ota.replace(marker, "    has_new_version_ = false; // This custom pet is updated by its host tools only.\n" + marker, 1)
    marker = 'bool Ota::Upgrade(const std::string& firmware_url, std::function<void(int progress, size_t speed)> callback) {'
    assert marker in ota
    ota = ota.replace(marker, marker + '\n    ESP_LOGW(TAG, "Firmware updates are managed by the badge host tools");\n    return false;\n')
    # The shared pet saves UTC epochs and the RTC adapter sets TZ=CST-8. Upstream
    # pre-adds the timezone offset to the epoch, which would shift time twice and
    # falsely age the pet by eight hours when the cloud clock arrives.
    ota = ota.replace('ts += (timezone_offset->valueint * 60 * 1000);',
        '// TZ handles local display; retain the true UTC timestamp here.')
    write("main/ota.cc", ota)
    application = original("main/application.cc")
    application = application.replace('std::string download_url = settings.GetString("download_url");',
        'std::string download_url; // Shared stock assets are preserved by this build.')
    application = application.replace('display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());',
        'display->SetChatMessage("system", "按一下按键，和我说话");')
    write("main/application.cc", application)
    cmake = original("CMakeLists.txt").replace('set(PROJECT_VER "2.3.0")', 'set(PROJECT_VER "2.3.0-clawd.1")').replace("project(xiaozhi)", "project(clawd-pet)")
    write("CMakeLists.txt", cmake)
    shutil.copy2(ROOT / "system/partitions.csv", dest / "partitions/clawd-pet.csv")
    defaults = original("sdkconfig.defaults") + "\n" + original("sdkconfig.defaults.esp32s3")
    defaults += "\n" + (overlay / "sdkconfig.defaults").read_text()
    write("sdkconfig.defaults", defaults)
    # Existing local configurations must also select the correct board explicitly.
    sdk = dest / "sdkconfig"
    if sdk.exists():
        import re
        text = sdk.read_text(encoding="utf-8")
        selection = re.findall(r'^CONFIG_BOARD_TYPE_\w+=y$', text, flags=re.M)
        wanted = 'CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y'
        if selection != [wanted]:
            text = re.sub(r'^CONFIG_BOARD_TYPE_\w+=y$', '', text, flags=re.M)
            text += '\n' + wanted + '\nCONFIG_USE_DEVICE_AEC=y\n'
            sdk.write_text(text, encoding="utf-8")
    print(f"Prepared CLAWD PET voice source: {dest}")
    print("Build with ESP-IDF 5.5.3; source preparation never writes to the device.")

if __name__ == "__main__":
    main()

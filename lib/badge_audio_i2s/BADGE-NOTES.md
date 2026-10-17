ESP32-audioI2S source header identifies version 3.0.8s, March 23 2024.
Copied from the locally installed library; original GPL-3.0 license and decoder notices retained.
Badge changes: unique BadgeAudio.h/BadgeAudio.cpp names; 256fs MCLK for ES8311; disable the IDF 5 I2S channel before deleting it.
Diagnostics count complete stereo frames/nonzero frames accepted by I2S, and non-timeout write errors. These are distinct from pre-output callback attempts.
The badge's PCM observer sets continueI2S=true; leaving the default false would consume samples without hardware output. The app pumps this synchronous decoder from a dedicated core-0 task, serializing controls and joining it before releasing the driver/card.

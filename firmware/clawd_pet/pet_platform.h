#pragma once
#include "driver/i2c_master.h"
namespace clawd {
void BeginPlatform(i2c_master_bus_handle_t bus);
void PollInput();
void ReturnToLauncher();
}

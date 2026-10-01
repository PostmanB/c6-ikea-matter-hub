#pragma once
#include <lib/core/DataModelTypes.h>
#include <nvs.h>
void lighting_init(nvs_handle_t storage, void (*result)(bool));
void lighting_tick(chip::EndpointId endpoint, bool canSend);
bool lighting_busy();
bool lighting_ready();
void lighting_status();
void lighting_cancel();
void lighting_hold(int slot, int button, bool pressed);
bool lighting_level(unsigned percent);
bool lighting_color(int direction);


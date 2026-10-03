#pragma once
#include <esp_err.h>
#include "lighting.h"
struct PhoneCommand {
    int on = -1, level = -1;
    float hue = -1, saturation = -1;
};
void network_home_init();
void network_home_tick(void (*turnOn)(bool));
void network_home_publish(const LightingSnapshot &state);
bool network_home_command(PhoneCommand &command);
bool network_home_console(int argc, char **argv, esp_err_t &result);
void network_home_status();

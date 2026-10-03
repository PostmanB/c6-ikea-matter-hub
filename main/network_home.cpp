#include "network_home.h"
#include "daily_schedule.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <ctime>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <mbedtls/bignum.h>
#include <mbedtls/sha512.h>
#include <mbedtls/platform_util.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <hap.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>

namespace {
const char *TAG = "home";
const char *hubTimezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
struct Settings {
    uint32_t version = 1;
    char ssid[33]{}, password[65]{};
    DailySchedule schedule;
    uint32_t homeConfigured = 0;
    hap_setup_info_t setup{};
    char setupId[5]{};
};
Settings settings;
nvs_handle_t configStore = 0;
bool storageOK = false;
std::atomic<bool> ipReady{false}, clockReady{false}, homeStarted{false};
QueueHandle_t phoneQueue;
portMUX_TYPE snapshotLock = portMUX_INITIALIZER_UNLOCKED;
LightingSnapshot snapshot;
hap_char_t *onChar, *levelChar, *hueChar, *satChar;

LightingSnapshot copySnapshot() {
    portENTER_CRITICAL(&snapshotLock);
    const auto value = snapshot;
    portEXIT_CRITICAL(&snapshotLock);
    return value;
}
esp_err_t saveSettings(const Settings &candidate) {
    if (!storageOK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_blob(configStore, "settings", &candidate, sizeof(candidate));
    if (err == ESP_OK) err = nvs_commit(configStore);
    if (err == ESP_OK) settings = candidate;
    else ESP_LOGE(TAG, "Settings save failed: %s; previous configuration retained", esp_err_to_name(err));
    return err;
}
bool unhex(const char *text, char *out, size_t maximum, size_t &length) {
    const size_t n = strlen(text);
    if (n % 2 || n / 2 > maximum) return false;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < n; i += 2) {
        int a = digit(text[i]), b = digit(text[i+1]);
        if (a < 0 || b < 0 || !(out[i/2] = char(a * 16 + b))) return false;
    }
    length = n / 2; out[length] = 0;
    return true;
}
void syncTime(timeval *) {
    clockReady = true;
    ESP_LOGI(TAG, "Clock synchronized; daily schedules enabled when configured");
}
void networkEvent(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ipReady = false;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        ipReady = true;
        ESP_LOGI(TAG, "Wi-Fi connected; IP=" IPSTR, IP2STR(&event->ip_info.ip));
    }
}
int identify(hap_acc_t *) { ESP_LOGI(TAG, "Apple Home identify request"); return HAP_SUCCESS; }
int readCharacteristic(hap_char_t *hc, hap_status_t *status, void *, void *) {
    auto state = copySnapshot();
    if (!state.online) { *status = HAP_STATUS_COMM_ERR; return HAP_FAIL; }
    hap_val_t value{};
    if (hc == onChar) value.b = state.on;
    else if (hc == levelChar) value.i = state.level;
    else if (hc == hueChar) value.f = state.hue;
    else if (hc == satChar) value.f = state.saturation;
    else { *status = HAP_STATUS_SUCCESS; return HAP_SUCCESS; }
    hap_char_update_val(hc, &value);
    *status = HAP_STATUS_SUCCESS;
    return HAP_SUCCESS;
}
int writeCharacteristics(hap_write_data_t writes[], int count, void *, void *) {
    const auto state = copySnapshot();
    PhoneCommand command;
    bool valid = state.online;
    for (int i = 0; i < count; ++i) {
        const auto &w = writes[i];
        if (w.hc == onChar) command.on = w.val.b;
        else if (w.hc == levelChar) { command.level = w.val.i; valid &= command.level >= 0 && command.level <= 100; }
        else if (w.hc == hueChar) { command.hue = w.val.f; valid &= state.rgb && std::isfinite(command.hue) && command.hue >= 0 && command.hue <= 360; }
        else if (w.hc == satChar) { command.saturation = w.val.f; valid &= state.rgb && std::isfinite(command.saturation) && command.saturation >= 0 && command.saturation <= 100; }
        else valid = false;
    }
    if (command.hue >= 0 && command.saturation < 0) command.saturation = state.saturation;
    if (command.saturation >= 0 && command.hue < 0) command.hue = state.hue;
    // HomeKit callbacks never take the CHIP lock. A bounded mailbox avoids a
    // HomeKit/CHIP lock inversion while remote and bulb subscriptions report.
    const bool accepted = valid && xQueueSend(phoneQueue, &command, 0) == pdTRUE;
    for (int i = 0; i < count; ++i)
        *writes[i].status = accepted ? HAP_STATUS_SUCCESS : !state.online ? HAP_STATUS_COMM_ERR : valid ? HAP_STATUS_RES_BUSY : HAP_STATUS_VAL_INVALID;
    return accepted ? HAP_SUCCESS : HAP_FAIL;
}
bool startHome(const Settings &boot) {
    if (!boot.homeConfigured) return false;
    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) return false;
    if (hap_set_setup_info(&boot.setup) != HAP_SUCCESS || hap_set_setup_id(boot.setupId) != HAP_SUCCESS) return false;
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char serial[13]; snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    hap_acc_cfg_t cfg{};
    cfg.name = const_cast<char *>("C6 IKEA Light"); cfg.manufacturer = const_cast<char *>("DIY");
    cfg.model = const_cast<char *>("C6 Matter Hub"); cfg.serial_num = serial;
    cfg.fw_rev = const_cast<char *>("0.2.0"); cfg.hw_rev = const_cast<char *>("ESP32-C6"); cfg.pv = const_cast<char *>("1.1.0");
    cfg.cid = HAP_CID_LIGHTING; cfg.identify_routine = identify;
    hap_acc_t *accessory = hap_acc_create(&cfg);
    hap_serv_t *service = hap_serv_lightbulb_create(false);
    if (!accessory || !service) return false;
    onChar = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_ON);
    levelChar = hap_char_brightness_create(0); hueChar = hap_char_hue_create(0); satChar = hap_char_saturation_create(0);
    if (!onChar || !levelChar || !hueChar || !satChar) return false;
    hap_serv_add_char(service, levelChar); hap_serv_add_char(service, hueChar); hap_serv_add_char(service, satChar);
    hap_serv_set_write_cb(service, writeCharacteristics); hap_serv_set_read_cb(service, readCharacteristic);
    hap_acc_add_serv(accessory, service); hap_add_accessory(accessory);
    if (hap_start() != HAP_SUCCESS) return false;
    ESP_LOGI(TAG, "Apple Home ready: Add Accessory > More Options > C6 IKEA Light > Add Anyway; use your chosen HomeKit PIN");
    return true;
}
void networkTask(void *argument) {
    const Settings boot = *static_cast<Settings *>(argument);
    delete static_cast<Settings *>(argument);
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    }
    if (err == ESP_OK && !esp_netif_create_default_wifi_sta()) err = ESP_ERR_NO_MEM;
    wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) err = esp_wifi_init(&wifiInit);
    // Keep Wi-Fi credentials out of the Thread NVS partition; our own settings
    // blob in home_nvs is the sole persistent source.
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, networkEvent, nullptr);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, networkEvent, nullptr);
    wifi_config_t config{};
    memcpy(config.sta.ssid, boot.ssid, strlen(boot.ssid)); memcpy(config.sta.password, boot.password, strlen(boot.password));
    config.sta.threshold.authmode = boot.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = esp_wifi_set_ps(WIFI_PS_NONE);
    mbedtls_platform_zeroize(config.sta.password, sizeof(config.sta.password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Optional Wi-Fi failed: %s; Thread control remains active", esp_err_to_name(err));
        vTaskDelete(nullptr); return;
    }
    esp_sntp_config_t timeConfig = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    timeConfig.start = false; timeConfig.sync_cb = syncTime;
    err = esp_netif_sntp_init(&timeConfig);
    bool timeStarted = false, homeAttempted = false;
    unsigned reconnectSeconds = 0;
    while (true) {
        if (!ipReady && reconnectSeconds++ % 15 == 0) esp_wifi_connect();
        if (ipReady) {
            reconnectSeconds = 0;
            if (err == ESP_OK && !timeStarted) { timeStarted = esp_netif_sntp_start() == ESP_OK; }
            if (boot.homeConfigured && !homeAttempted) { homeAttempted = true; homeStarted = startHome(boot); }
            if (homeStarted) {
                auto state = copySnapshot();
                if (state.online) {
                    hap_val_t value{};
                    value.b = state.on; hap_char_update_val(onChar, &value);
                    value.i = state.level; hap_char_update_val(levelChar, &value);
                    value.f = state.hue; hap_char_update_val(hueChar, &value);
                    value.f = state.saturation; hap_char_update_val(satChar, &value);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
// Generate an SRP salt/verifier using the SDK's standard 3072-bit group. The
// raw PIN only exists during this calculation and is never persisted.
// RFC 5054 group modulus is defined below by the companion generated header.
#include "srp_group.h"
bool makeSetup(const char *digits, hap_setup_info_t &out) {
    if (strlen(digits) != 8 || !strcmp(digits, "12345678") || !strcmp(digits, "87654321")) return false;
    bool repeated = true;
    for (int i = 0; i < 8; ++i) { if (digits[i] < '0' || digits[i] > '9') return false; repeated &= digits[i] == digits[0]; }
    if (repeated) return false;
    char secret[30]; snprintf(secret, sizeof(secret), "Pair-Setup:%.3s-%.2s-%.3s", digits, digits+3, digits+5);
    uint8_t inner[64], xBytes[64], input[80];
    esp_fill_random(out.salt, sizeof(out.salt));
    bool ok = mbedtls_sha512(reinterpret_cast<const uint8_t *>(secret), strlen(secret), inner, 0) == 0;
    memcpy(input, out.salt, 16); memcpy(input + 16, inner, 64);
    ok &= mbedtls_sha512(input, sizeof(input), xBytes, 0) == 0;
    mbedtls_mpi n, g, x, v;
    mbedtls_mpi_init(&n); mbedtls_mpi_init(&g); mbedtls_mpi_init(&x); mbedtls_mpi_init(&v);
    ok &= mbedtls_mpi_read_string(&n, 16, srpGroupHex) == 0 && mbedtls_mpi_lset(&g, 5) == 0 &&
        mbedtls_mpi_read_binary(&x, xBytes, sizeof(xBytes)) == 0 && mbedtls_mpi_exp_mod(&v, &g, &x, &n, nullptr) == 0 &&
        mbedtls_mpi_write_binary(&v, out.verifier, sizeof(out.verifier)) == 0;
    mbedtls_mpi_free(&n); mbedtls_mpi_free(&g); mbedtls_mpi_free(&x); mbedtls_mpi_free(&v);
    mbedtls_platform_zeroize(secret, sizeof(secret)); mbedtls_platform_zeroize(inner, sizeof(inner));
    mbedtls_platform_zeroize(xBytes, sizeof(xBytes)); mbedtls_platform_zeroize(input, sizeof(input));
    return ok;
}
}

void network_home_init() {
    setenv("TZ", hubTimezone, 1); tzset();
    phoneQueue = xQueueCreate(4, sizeof(PhoneCommand));
    esp_err_t err = nvs_flash_init_partition("home_nvs");
    if (err == ESP_OK) err = nvs_open_from_partition("home_nvs", "config", NVS_READWRITE, &configStore);
    if (err == ESP_OK) {
        size_t size = sizeof(settings); Settings restored;
        err = nvs_get_blob(configStore, "settings", &restored, &size);
        if (err == ESP_OK && size == sizeof(restored) && restored.version == 1 && restored.schedule.version == 1 &&
            restored.schedule.hour < 24 && restored.schedule.minute < 60 && memchr(restored.ssid, 0, sizeof(restored.ssid)) &&
            memchr(restored.password, 0, sizeof(restored.password))) { settings = restored; storageOK = true; }
        else if (err == ESP_ERR_NVS_NOT_FOUND) storageOK = true;
    }
    if (!storageOK || !phoneQueue) { ESP_LOGE(TAG, "Optional settings unavailable; no storage erased; existing Thread hub continues"); return; }
    if (!settings.ssid[0]) { ESP_LOGI(TAG, "Wi-Fi unconfigured; offline Thread control active"); return; }
    auto *boot = new (std::nothrow) Settings(settings);
    if (!boot || xTaskCreate(networkTask, "hub_network", 8192, boot, 2, nullptr) != pdPASS) {
        delete boot; ESP_LOGE(TAG, "Optional network task unavailable");
    }
}
void network_home_publish(const LightingSnapshot &state) {
    portENTER_CRITICAL(&snapshotLock); snapshot = state; portEXIT_CRITICAL(&snapshotLock);
}
bool network_home_command(PhoneCommand &command) { return phoneQueue && xQueueReceive(phoneQueue, &command, 0) == pdTRUE; }
void network_home_tick(void (*turnOn)(bool)) {
    const time_t epoch = time(nullptr); tm local{}; localtime_r(&epoch, &local);
    if (!settings.schedule.due(local, clockReady && epoch >= 1735689600LL && epoch < 4102444800LL)) return;
    Settings fired = settings; fired.schedule.mark(local);
    // Persist the day before dispatch. Reboot cannot replay the occurrence; On
    // already has bounded Matter ACK retries. A failed save suppresses dispatch.
    if (saveSettings(fired) != ESP_OK) return;
    ESP_LOGI(TAG, "Daily schedule: %02u:%02u On (%08ld)", settings.schedule.hour, settings.schedule.minute, (long)settings.schedule.lastDay);
    turnOn(true);
}
void network_home_status() {
    char timeText[32] = "unsynchronized";
    if (clockReady) { time_t epoch = time(nullptr); tm local{}; localtime_r(&epoch, &local); strftime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S %Z", &local); }
    ESP_LOGI(TAG, "Clock=%s zone=Europe/Bratislava schedule=%s %02u:%02u lastDay=%ld", timeText,
        settings.schedule.enabled ? "on" : "off", settings.schedule.hour, settings.schedule.minute, (long)settings.schedule.lastDay);
    ESP_LOGI(TAG, "Wi-Fi configured=%u connected=%u HomeKit configured=%u running=%u optionalStorage=%s",
        bool(settings.ssid[0]), bool(ipReady), bool(settings.homeConfigured), bool(homeStarted), storageOK ? "ok" : "ERROR");
}
bool network_home_console(int argc, char **argv, esp_err_t &result) {
    if (!argc || (strcmp(argv[0], "wifi") && strcmp(argv[0], "home") && strcmp(argv[0], "schedule") && strcmp(argv[0], "clock") && strcmp(argv[0], "restart"))) return false;
    result = ESP_ERR_INVALID_ARG;
    if (argc == 1 && (!strcmp(argv[0], "clock") || !strcmp(argv[0], "home"))) { network_home_status(); result = ESP_OK; return true; }
    if (argc == 1 && !strcmp(argv[0], "restart")) { ESP_LOGI(TAG, "Restarting with saved pairings and settings"); esp_restart(); }
    Settings candidate = settings;
    if (argc == 3 && !strcmp(argv[0], "wifi")) {
        size_t ssidLength, passwordLength;
        memset(candidate.ssid, 0, sizeof(candidate.ssid)); memset(candidate.password, 0, sizeof(candidate.password));
        if (!unhex(argv[1], candidate.ssid, 32, ssidLength) || !ssidLength ||
            (strcmp(argv[2], "-") && !unhex(argv[2], candidate.password, 64, passwordLength)) ||
            (candidate.password[0] && strlen(candidate.password) < 8)) return true;
        if (strlen(candidate.password) == 64) {
            for (char c : candidate.password) {
                if (c && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return true;
            }
        }
        result = saveSettings(candidate);
        mbedtls_platform_zeroize(candidate.password, sizeof(candidate.password));
        if (result == ESP_OK) ESP_LOGI(TAG, "Wi-Fi configuration saved; restart to apply");
    } else if (argc == 2 && !strcmp(argv[0], "home")) {
        // Protect an existing Apple Home identity: changing its PIN requires a
        // deliberate separate maintenance workflow, never a setup page reload.
        if (settings.homeConfigured) { result = ESP_ERR_INVALID_STATE; return true; }
        if (!makeSetup(argv[1], candidate.setup)) return true;
        uint32_t id = esp_random();
        const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int i = 0; i < 4; ++i) { candidate.setupId[i] = chars[id % 36]; id /= 36; }
        candidate.homeConfigured = 1;
        result = saveSettings(candidate);
        if (result == ESP_OK) ESP_LOGI(TAG, "HomeKit verifier saved; note your PIN; restart to apply");
    } else if (argc == 2 && !strcmp(argv[0], "schedule")) {
        if (!strcmp(argv[1], "off")) candidate.schedule.enabled = 0;
        else {
            if (strlen(argv[1]) != 5 || argv[1][2] != ':' || argv[1][0] < '0' || argv[1][0] > '2' || argv[1][1] < '0' || argv[1][1] > '9' ||
                argv[1][3] < '0' || argv[1][3] > '5' || argv[1][4] < '0' || argv[1][4] > '9') return true;
            unsigned hour = (argv[1][0] - '0') * 10 + argv[1][1] - '0', minute = (argv[1][3] - '0') * 10 + argv[1][4] - '0';
            if (hour > 23) return true;
            candidate.schedule.hour = hour; candidate.schedule.minute = minute; candidate.schedule.enabled = 1;
        }
        result = saveSettings(candidate); if (result == ESP_OK) network_home_status();
    }
    return true;
}

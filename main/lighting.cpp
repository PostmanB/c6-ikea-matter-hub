#include "lighting.h"
#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_matter_controller_client.h>
#include <app/BufferedReadCallback.h>
#include <app/InteractionModelEngine.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <controller/InvokeInteraction.h>

using namespace chip;
using namespace chip::app;
namespace {
constexpr NodeId bulbNode = 0x100;
constexpr ClusterId levelCluster = 8, colorCluster = 0x300;
const char *TAG = "lighting";
int64_t now() { return esp_timer_get_time(); }
auto &controller() {
    return *esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
}
nvs_handle_t storage;
void (*countResult)(bool) = nullptr;
EndpointId endpoint = kInvalidEndpointId;
bool haveLevel = false, haveFeatures = false, haveCapabilities = false;
uint32_t features = 0, capabilities = 0;
int currentLevel = 254, minimumLevel = 1, maximumLevel = 254;
int minimumMired = 153, maximumMired = 555;
bool currentOn = false, haveOn = false;
unsigned currentHue = 0, currentSaturation = 0;
unsigned paletteIndex = 1;
struct Preset { const char *name; uint16_t mired, x, y; uint8_t hue, saturation; };
constexpr Preset palette[] = {
    {"warm 2200K",455,0,0,0,0}, {"warm 2700K",370,0,0,0,0},
    {"neutral 4000K",250,0,0,0,0}, {"cool 6500K",154,0,0,0,0},
    {"red",0,44564,20316,0,254}, {"green",0,13762,46530,85,254},
    {"blue",0,9175,3932,169,254}, {"purple",0,22282,10485,211,254},
    {"pink",0,29491,18350,233,180}
};
uint32_t colorSupport() {
    return haveFeatures && haveCapabilities ? features & capabilities :
        haveFeatures ? features : haveCapabilities ? capabilities : 0;
}
bool presetSupported(unsigned index) {
    return palette[index].mired ? (colorSupport() & 0x10) : (colorSupport() & 0x9);
}
enum class Kind { None, Level, Color, HueSaturation, LevelOnly };
struct Command { Kind kind = Kind::None; unsigned value = 0; uint32_t generation = 0; };
Command pending, sent;
Command following;
uint32_t generation = 0;
bool sending = false;
unsigned attempts = 0;
int64_t commandDue = 0;
int holdSlot = -1, holdButton = -1;
int64_t holdUntil = 0, holdNext = 0;
void queue(Kind kind, unsigned value) {
    pending = {kind, value, ++generation};
    attempts = 0;
    commandDue = now();
}

class StateReader final : public ReadClient::Callback {
public:
    bool busy = false, live = false, done = false;
    int64_t due = 0;
    std::unique_ptr<ReadClient> client;
    BufferedReadCallback buffered{*this};
    std::array<AttributePathParams, 15> paths;
    chip::Callback::Callback<OnDeviceConnected> connected{onConnected, this};
    chip::Callback::Callback<OnDeviceConnectionFailure> failed{onFailed, this};
    static void onFailed(void *ctx, const ScopedNodeId &, CHIP_ERROR err) {
        auto &s = *static_cast<StateReader *>(ctx);
        s.busy = s.live = false; s.done = true; s.due = now() + 15000000;
        ESP_LOGW(TAG, "Bulb state connection: %s", ErrorStr(err));
    }
    static void onConnected(void *ctx, Messaging::ExchangeManager &mgr, const SessionHandle &session) {
        auto &s = *static_cast<StateReader *>(ctx);
        s.client.reset(new (std::nothrow) ReadClient(InteractionModelEngine::GetInstance(), &mgr,
            s.buffered, ReadClient::InteractionType::Subscribe));
        if (!s.client) { onFailed(ctx, {}, CHIP_ERROR_NO_MEMORY); return; }
        const ClusterId clusters[] = {8,8,8,8,0x300,0x300,0x300,0x300,0x300,0x300,0x300,0x300,0x300,0x300,6};
        const AttributeId attrs[] = {0,2,3,0xfffc,0xfffc,0x400a,0x400b,0x400c,7,0,1,3,4,8,0};
        for (unsigned i = 0; i < s.paths.size(); ++i) s.paths[i] = AttributePathParams(endpoint, clusters[i], attrs[i]);
        ReadPrepareParams params(session);
        params.mpAttributePathParamsList = s.paths.data();
        params.mAttributePathParamsListSize = s.paths.size();
        params.mMinIntervalFloorSeconds = 0;
        params.mMaxIntervalCeilingSeconds = 60;
        params.mKeepSubscriptions = false;
        CHIP_ERROR err = s.client->SendRequest(params);
        if (err != CHIP_NO_ERROR) onFailed(ctx, {}, err);
    }
    void OnAttributeData(const ConcreteDataAttributePath &path, TLV::TLVReader *reader, const StatusIB &status) override {
        if (!reader || status.ToChipError() != CHIP_NO_ERROR || reader->GetType() == TLV::kTLVType_Null) return;
        if (path.mClusterId == 6 && path.mAttributeId == 0) {
            bool value;
            if (reader->Get(value) == CHIP_NO_ERROR) { currentOn = value; haveOn = true; }
            return;
        }
        uint32_t value;
        if (reader->Get(value) != CHIP_NO_ERROR) return;
        if (path.mClusterId == levelCluster) {
            if (path.mAttributeId == 0 && value <= 254) { currentLevel = value; haveLevel = true; }
            if (path.mAttributeId == 2 && value <= 254) minimumLevel = std::max<uint32_t>(1, value);
            if (path.mAttributeId == 3 && value >= 1 && value <= 254) maximumLevel = value;
        } else if (path.mClusterId == colorCluster) {
            if (path.mAttributeId == 0 && value <= 254) currentHue = value;
            if (path.mAttributeId == 1 && value <= 254) currentSaturation = value;
            if (path.mAttributeId == 0xfffc) { features = value; haveFeatures = true; }
            if (path.mAttributeId == 0x400a) { capabilities = value; haveCapabilities = true; }
            if (path.mAttributeId == 0x400b && value > 0 && value <= 65535) minimumMired = value;
            if (path.mAttributeId == 0x400c && value > 0 && value <= 65535) maximumMired = value;
        }
        ESP_LOGI(TAG, "Bulb attribute cluster=0x%lx attr=0x%lx value=%lu",
            (unsigned long)path.mClusterId, (unsigned long)path.mAttributeId, (unsigned long)value);
    }
    void OnSubscriptionEstablished(SubscriptionId) override { live = true; ESP_LOGI(TAG, "Bulb brightness/colour state subscribed"); }
    void OnError(CHIP_ERROR err) override { live = false; ESP_LOGW(TAG, "Bulb state subscription: %s", ErrorStr(err)); }
    void OnDone(ReadClient *) override { busy = live = false; done = true; due = now() + 2000000; }
    void tick() {
        if (done) { client.reset(); done = false; }
        if (busy || now() < due) return;
        busy = true;
        CHIP_ERROR err = controller().GetConnectedDevice(bulbNode, &connected, &failed);
        if (err != CHIP_NO_ERROR) onFailed(this, {}, err);
    }
};
StateReader state;
void result(CHIP_ERROR error) {
    sending = false;
    if (countResult) countResult(error == CHIP_NO_ERROR);
    if (error == CHIP_NO_ERROR) {
        if (sent.kind == Kind::Level || sent.kind == Kind::LevelOnly) currentLevel = sent.value;
        if (sent.kind == Kind::Color) {
            // Only colour changes write this small cursor; dimming never writes flash.
            if (nvs_set_u32(storage, "palette", sent.value) != ESP_OK || nvs_commit(storage) != ESP_OK)
                ESP_LOGW(TAG, "Palette cursor could not be saved");
        }
        ESP_LOGI(TAG, "Bulb acknowledged %s %u", sent.kind == Kind::Level ? "level" : "palette", sent.value);
        if (pending.generation == sent.generation) {
            pending = following;
            following.kind = Kind::None;
        }
        attempts = 0;
        commandDue = now();
    } else {
        ESP_LOGW(TAG, "Bulb lighting command: %s", ErrorStr(error));
        if (pending.generation == sent.generation && ++attempts >= 3) {
            pending.kind = Kind::None;
            following.kind = Kind::None;
            holdSlot = -1;
            ESP_LOGE(TAG, "Lighting stopped after three attempts");
        }
        commandDue = now() + 2000000;
    }
}
void connected(void *, Messaging::ExchangeManager &mgr, const SessionHandle &session) {
    auto ok = [](const ConcreteCommandPath &, const StatusIB &, const DataModel::NullObjectType &) { result(CHIP_NO_ERROR); };
    auto fail = [](CHIP_ERROR err) { result(err); };
    CHIP_ERROR err;
    if (sent.kind == Kind::LevelOnly) {
        Clusters::LevelControl::Commands::MoveToLevel::Type command;
        command.level = sent.value;
        command.transitionTime.SetNonNull(5);
        command.optionsMask.Set(Clusters::LevelControl::OptionsBitmap::kExecuteIfOff);
        command.optionsOverride.Set(Clusters::LevelControl::OptionsBitmap::kExecuteIfOff);
        err = Controller::InvokeCommandRequest(&mgr, session, endpoint, command, ok, fail);
    } else if (sent.kind == Kind::HueSaturation) {
        Clusters::ColorControl::Commands::MoveToHueAndSaturation::Type command;
        command.hue = sent.value >> 8; command.saturation = sent.value & 255;
        command.transitionTime = 5;
        command.optionsMask.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
        command.optionsOverride.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
        err = Controller::InvokeCommandRequest(&mgr, session, endpoint, command, ok, fail);
    } else if (sent.kind == Kind::Level) {
        Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Type command;
        command.level = sent.value;
        command.transitionTime.SetNonNull(5); // Half-second, finite transition.
        err = Controller::InvokeCommandRequest(&mgr, session, endpoint, command, ok, fail);
    } else {
        const auto &p = palette[sent.value];
        ESP_LOGI(TAG, "Colour preset: %s", p.name);
        if (p.mired) {
            Clusters::ColorControl::Commands::MoveToColorTemperature::Type command;
            command.colorTemperatureMireds = std::clamp<int>(p.mired, minimumMired, std::max(minimumMired, maximumMired));
            command.transitionTime = 5;
            command.optionsMask.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
            command.optionsOverride.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
            err = Controller::InvokeCommandRequest(&mgr, session, endpoint, command, ok, fail);
        } else if (colorSupport() & 1) {
            Clusters::ColorControl::Commands::MoveToHueAndSaturation::Type command;
            command.hue = p.hue; command.saturation = p.saturation; command.transitionTime = 5;
            command.optionsMask.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
            command.optionsOverride.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
            err = Controller::InvokeCommandRequest(&mgr, session, endpoint, command, ok, fail);
        } else {
            Clusters::ColorControl::Commands::MoveToColor::Type command;
            command.colorX = p.x; command.colorY = p.y; command.transitionTime = 5;
            command.optionsMask.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
            command.optionsOverride.Set(Clusters::ColorControl::OptionsBitmap::kExecuteIfOff);
            err = Controller::InvokeCommandRequest(&mgr, session, endpoint, command, ok, fail);
        }
    }
    if (err != CHIP_NO_ERROR) result(err);
}
void failed(void *, const ScopedNodeId &, CHIP_ERROR err) { result(err); }
Callback::Callback<OnDeviceConnected> connection{connected, nullptr};
Callback::Callback<OnDeviceConnectionFailure> failure{failed, nullptr};
void dimStep() {
    const int base = pending.kind == Kind::Level ? pending.value : currentLevel;
    const int target = std::clamp(base + (holdButton == 0 ? 32 : -32), minimumLevel, std::max(minimumLevel, maximumLevel));
    if (target != base) queue(Kind::Level, target);
}
} // namespace

void lighting_init(nvs_handle_t handle, void (*callback)(bool)) {
    storage = handle; countResult = callback;
    uint32_t saved;
    if (nvs_get_u32(storage, "palette", &saved) == ESP_OK && saved < std::size(palette)) paletteIndex = saved;
}
bool lighting_busy() { return sending; }
bool lighting_ready() { return state.live && haveLevel && (haveFeatures || haveCapabilities); }
void lighting_cancel() { holdSlot = -1; pending.kind = following.kind = Kind::None; }
LightingSnapshot lighting_snapshot() {
    LightingSnapshot snapshot;
    snapshot.online = state.live && haveLevel && haveOn;
    snapshot.on = currentOn; snapshot.level = (currentLevel * 100 + 127) / 254;
    snapshot.rgb = colorSupport() & 1;
    snapshot.hue = currentHue * 360.0f / 254; snapshot.saturation = currentSaturation * 100.0f / 254;
    return snapshot;
}
bool lighting_home(int percent, float hue, float saturation, bool levelTurnsOn) {
    if (!state.live || !haveLevel || percent > 100 || hue > 360 || saturation > 100) return false;
    if (hue >= 0 && !(colorSupport() & 1)) return false;
    lighting_cancel();
    if (percent >= 0) queue(levelTurnsOn ? Kind::Level : Kind::LevelOnly,
        percent == 0 ? 0 : std::clamp<int>((percent * 254 + 50) / 100, minimumLevel, std::max(minimumLevel, maximumLevel)));
    if (hue >= 0 && saturation >= 0) {
        Command colour{Kind::HueSaturation, (unsigned(hue * 254 / 360 + 0.5f) << 8) | unsigned(saturation * 254 / 100 + 0.5f), ++generation};
        if (pending.kind == Kind::None) pending = colour; else following = colour;
        commandDue = now(); attempts = 0;
    }
    return true;
}
void lighting_hold(int slot, int button, bool pressed) {
    if (!pressed) {
        if (holdSlot == slot && holdButton == button) {
            holdSlot = -1;
            if (pending.kind == Kind::Level) pending.kind = Kind::None;
            ESP_LOGI(TAG, "Hold released; finite dimming stopped");
        }
        return;
    }
    if (!state.live || !haveLevel) { ESP_LOGW(TAG, "Brightness state not ready"); return; }
    lighting_cancel();
    holdSlot = slot; holdButton = button;
    holdUntil = now() + 15000000; // Lost release cannot leave continuous motion running.
    holdNext = now() + 1000000;
    dimStep();
    ESP_LOGI(TAG, "Remote %d hold: %s", slot, button == 0 ? "brighten" : "dim");
}
bool lighting_level(unsigned percent) {
    if (!state.live || !haveLevel || percent > 100) return false;
    lighting_cancel();
    queue(Kind::Level, percent == 0 ? 0 : std::clamp<int>((percent * 254 + 50) / 100, minimumLevel, std::max(minimumLevel, maximumLevel)));
    return true;
}
bool lighting_color(int direction) {
    if (!state.live || !colorSupport()) return false;
    lighting_cancel();
    for (unsigned i = 0; i < std::size(palette); ++i) {
        paletteIndex = (paletteIndex + std::size(palette) + (direction > 0 ? 1 : -1)) % std::size(palette);
        if (presetSupported(paletteIndex)) { queue(Kind::Color, paletteIndex); return true; }
    }
    return false;
}
void lighting_status() {
    ESP_LOGI(TAG, "State subscribed=%u level=%d range=%d..%d colorFeatures=0x%lx capabilities=0x%lx RGB=%u white=%u mired=%d..%d preset=%s holding=%d",
        state.live, currentLevel, minimumLevel, maximumLevel, (unsigned long)features,
        (unsigned long)capabilities, bool(colorSupport() & 9), bool(colorSupport() & 0x10),
        minimumMired, maximumMired, palette[paletteIndex].name, holdSlot);
}
void lighting_tick(EndpointId ep, bool canSend) {
    if (endpoint != ep) { endpoint = ep; state.client.reset(); state.busy = state.live = false; state.due = 0; }
    if (ep == kInvalidEndpointId) { lighting_cancel(); return; }
    state.tick();
    if (holdSlot >= 0) {
        if (now() >= holdUntil || !state.live) lighting_cancel();
        else if (now() >= holdNext) {
            holdNext += 1000000;
            // Keep each target stable through retries rather than moving it away.
            if (!sending && pending.kind == Kind::None) dimStep();
        }
    }
    if (!canSend || sending || pending.kind == Kind::None || now() < commandDue) return;
    sent = pending; sending = true;
    CHIP_ERROR err = controller().GetConnectedDevice(bulbNode, &connection, &failure);
    if (err != CHIP_NO_ERROR) result(err);
}

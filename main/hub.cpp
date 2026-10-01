#include "credentials.h"
#include "lighting.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <driver/usb_serial_jtag.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/ip6.h>
#include <openthread/srp_server.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <app/BufferedReadCallback.h>
#include <app/InteractionModelEngine.h>
#include <app/data-model/Decode.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <controller/InvokeInteraction.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/OpenthreadLauncher.h>

using namespace chip;
using namespace chip::app;
namespace ctrl = esp_matter::controller;
static const char *TAG = "hub";
static constexpr ClusterId kSwitch = 0x003b, kDescriptor = 0x001d, kOnOff = 6;
static constexpr EventId kInitialPress = 1;
static const char *names[] = { "bulb", "door", "desk", "bedside" };
static int64_t seconds() { return esp_timer_get_time() / 1000000; }
static auto &commissioner() { return *ctrl::matter_controller_client::get_instance().get_commissioner(); }

// Slot addresses stay stable for the lifetime of every asynchronous callback.
struct Record {
    uint32_t version = 1;
    uint8_t paired = 0;
    uint8_t reserved = 0;
    EndpointId on = kInvalidEndpointId, off = kInvalidEndpointId;
};
static std::array<Record, 4> records;
static nvs_handle_t store;
static bool storageHealthy = true;
static int pairingSlot = -1;
static int64_t discoveryDeadline = 0;
static int desired = -1, sent = -1;
static bool sending = false;
static uint8_t commandAttempts = 0;
static int64_t commandDue = 0;
enum class ReadySignal { Waiting, Flashing, Done, Cancelled, Failed };
static ReadySignal readySignal = ReadySignal::Waiting;
static unsigned readyStep = 0;
static int64_t readyCommandDue = 0;
static const char *readySignalName() {
    switch (readySignal) {
    case ReadySignal::Waiting: return "waiting";
    case ReadySignal::Flashing: return "flashing";
    case ReadySignal::Done: return "done";
    case ReadySignal::Cancelled: return "skipped by user command";
    case ReadySignal::Failed: return "bulb unavailable";
    }
    return "unknown";
}
static constexpr NodeId nodeFor(int slot) { return 0x100 + slot; }

// Ten bounded snapshots per boot let us diagnose charger operation after the
// next power cycle. This is not a continuously written flash log.
struct BootTrace {
    uint32_t version = 1, boot = 0, reset = 0, stage = 0, uptime = 0;
    uint32_t host = 0, role = 0, srp = 0, paired = 0, listening = 0;
    uint32_t buttons[3]{}, acknowledged = 0, failed = 0;
};
static BootTrace trace;
static unsigned traceSample = 0;
static constexpr uint32_t traceTimes[] = {5, 15, 30, 45, 60, 90, 120, 180, 240, 300};
static vprintf_like_t consolePrinter = &vprintf;
static int connectedLog(const char *format, va_list args) {
    // A charger has no USB host to drain diagnostic output.
    return usb_serial_jtag_is_connected() ? consolePrinter(format, args) : 0;
}
static void saveTrace() {
    trace.uptime = static_cast<uint32_t>(seconds());
    trace.host = usb_serial_jtag_is_connected();
    esp_err_t err = nvs_set_blob(store, "boottrace", &trace, sizeof(trace));
    if (err == ESP_OK) err = nvs_commit(store);
    if (err != ESP_OK) ESP_LOGW(TAG, "Startup trace save failed: %s", esp_err_to_name(err));
}
static void traceStage(unsigned stage) { trace.stage = stage; saveTrace(); }
static void printTrace(const char *label, const BootTrace &t) {
    ESP_LOGI(TAG, "%s boot=%lu reset=%lu stage=%lu uptime=%lu host=%lu role=%lu SRP=%lu paired=0x%lx listening=0x%lx buttons=%lu/%lu/%lu ack=%lu fail=%lu",
        label, (unsigned long)t.boot, (unsigned long)t.reset, (unsigned long)t.stage,
        (unsigned long)t.uptime, (unsigned long)t.host, (unsigned long)t.role,
        (unsigned long)t.srp, (unsigned long)t.paired, (unsigned long)t.listening,
        (unsigned long)t.buttons[0], (unsigned long)t.buttons[1], (unsigned long)t.buttons[2],
        (unsigned long)t.acknowledged, (unsigned long)t.failed);
}

static bool save(int slot) {
    esp_err_t err = nvs_set_blob(store, names[slot], &records[slot], sizeof(Record));
    if (err == ESP_OK) err = nvs_commit(store);
    if (err != ESP_OK) {
        storageHealthy = false;
        ESP_LOGE(TAG, "Cannot persist %s: %s; pairing disabled", names[slot], esp_err_to_name(err));
    }
    return err == ESP_OK;
}
static void cancelReadySignal() {
    if (readySignal == ReadySignal::Waiting || readySignal == ReadySignal::Flashing) {
        readySignal = ReadySignal::Cancelled;
        desired = -1;
        ESP_LOGI(TAG, "Startup flashes skipped for user control");
    }
}
static void requestState(bool on) {
    if (!records[0].paired || records[0].on == kInvalidEndpointId) {
        ESP_LOGW(TAG, "Bulb endpoint is not ready");
        return;
    }
    // A real button press or console command always takes priority over the
    // startup indication, including an indication waiting for sleepy remotes.
    cancelReadySignal();
    lighting_cancel();
    desired = on ? 1 : 0;
    commandAttempts = 0;
    commandDue = seconds();
}

class Peer final : public ReadClient::Callback {
public:
    explicit Peer(int index) : slot(index), buffered(*this), connected(onConnected, this), failed(onFailed, this) {}
    int slot;
    bool busy = false, live = false, discovery = false, finished = false, restart = false;
    bool discoveryFailed = false;
    int64_t due = 0;
    std::unique_ptr<ReadClient> client;
    BufferedReadCallback buffered;
    AttributePathParams attribute;
    EventPathParams event;
    std::array<EndpointId, 8> endpoints{};
    size_t count = 0;
    std::array<EventNumber, 2> last{};
    std::array<bool, 2> seen{};
    std::array<uint32_t, 2> switchFeatures{};
    chip::Callback::Callback<OnDeviceConnected> connected;
    chip::Callback::Callback<OnDeviceConnectionFailure> failed;

    void begin(bool scan) {
        // Destruction is deferred until outside ReadClient callback execution.
        client.reset();
        discovery = scan;
        discoveryFailed = finished = live = false;
        count = 0;
        seen.fill(false);
        busy = true;
        CHIP_ERROR err = commissioner().GetConnectedDevice(nodeFor(slot), &connected, &failed);
        if (err != CHIP_NO_ERROR) connectionError(err);
    }
    void connectionError(CHIP_ERROR error) {
        ESP_LOGW(TAG, "%s connection: %s; wake remote to retry", names[slot], ErrorStr(error));
        busy = false;
        live = false;
        due = seconds() + 15;
    }
    static void onFailed(void *ctx, const ScopedNodeId &, CHIP_ERROR error) {
        static_cast<Peer *>(ctx)->connectionError(error);
    }
    static void onConnected(void *ctx, Messaging::ExchangeManager &mgr, const SessionHandle &session) {
        auto &p = *static_cast<Peer *>(ctx);
        p.client.reset(new (std::nothrow) ReadClient(InteractionModelEngine::GetInstance(), &mgr,
            p.buffered, p.discovery ? ReadClient::InteractionType::Read : ReadClient::InteractionType::Subscribe));
        if (!p.client) { p.connectionError(CHIP_ERROR_NO_MEMORY); return; }
        ReadPrepareParams params(session);
        if (p.discovery) {
            // Read ServerList on every endpoint. No guessed endpoint numbers.
            p.attribute = AttributePathParams(kInvalidEndpointId, kDescriptor, 1);
            params.mpAttributePathParamsList = &p.attribute;
            params.mAttributePathParamsListSize = 1;
        } else {
            p.event = EventPathParams(kInvalidEndpointId, kSwitch, kInvalidEventId);
            p.event.mIsUrgentEvent = true;
            p.attribute = AttributePathParams(kInvalidEndpointId, kSwitch, 0xfffc);
            params.mpAttributePathParamsList = &p.attribute;
            params.mAttributePathParamsListSize = 1;
            params.mpEventPathParamsList = &p.event;
            params.mEventPathParamsListSize = 1;
            params.mMinIntervalFloorSeconds = 0;
            params.mMaxIntervalCeilingSeconds = 300;
            params.mKeepSubscriptions = false;
        }
        CHIP_ERROR err = p.client->SendRequest(params);
        if (err != CHIP_NO_ERROR) p.connectionError(err);
    }
    void OnAttributeData(const ConcreteDataAttributePath &path, TLV::TLVReader *reader,
                         const StatusIB &status) override {
        if (!discovery) {
            if (reader && status.ToChipError() == CHIP_NO_ERROR && path.mClusterId == kSwitch && path.mAttributeId == 0xfffc) {
                int button = path.mEndpointId == records[slot].on ? 0 : path.mEndpointId == records[slot].off ? 1 : -1;
                uint32_t features;
                if (button >= 0 && reader->Get(features) == CHIP_NO_ERROR) {
                    switchFeatures[button] = features;
                    ESP_LOGI(TAG, "%s ep=%u Switch features=0x%lx", names[slot], path.mEndpointId, (unsigned long)features);
                }
            }
            return;
        }
        if (!reader || status.ToChipError() != CHIP_NO_ERROR) { discoveryFailed = true; return; }
        if (path.mClusterId != kDescriptor || path.mAttributeId != 1) return;
        DataModel::DecodableList<ClusterId> clusters;
        if (DataModel::Decode(*reader, clusters) != CHIP_NO_ERROR) { discoveryFailed = true; return; }
        auto it = clusters.begin();
        bool match = false;
        while (it.Next()) {
            ESP_LOGI(TAG, "%s ep=%u server=0x%lx", names[slot], path.mEndpointId,
                     static_cast<unsigned long>(it.GetValue()));
            match |= it.GetValue() == (slot == 0 ? kOnOff : kSwitch);
        }
        if (it.GetStatus() != CHIP_NO_ERROR) { discoveryFailed = true; return; }
        if (match && std::find(endpoints.begin(), endpoints.begin() + count, path.mEndpointId) == endpoints.begin() + count) {
            if (count < endpoints.size()) endpoints[count++] = path.mEndpointId;
            else discoveryFailed = true;
        }
    }
    void OnEventData(const EventHeader &header, TLV::TLVReader *reader, const StatusIB *status) override {
        if (!reader || (status && status->ToChipError() != CHIP_NO_ERROR)) return;
        if (header.mPath.mClusterId != kSwitch || header.mPath.mEventId > 6) return;
        const auto &record = records[slot];
        const EndpointId ep = header.mPath.mEndpointId;
        int button = ep == record.on ? 0 : ep == record.off ? 1 : -1;
        if (button < 0) return;
        const EventId eventId = header.mPath.mEventId;
        unsigned presses = 0;
        CHIP_ERROR decoded = CHIP_ERROR_INVALID_ARGUMENT;
        switch (eventId) {
        case 1: { Clusters::Switch::Events::InitialPress::DecodableType v; decoded = DataModel::Decode(*reader, v); break; }
        case 2: { Clusters::Switch::Events::LongPress::DecodableType v; decoded = DataModel::Decode(*reader, v); break; }
        case 3: { Clusters::Switch::Events::ShortRelease::DecodableType v; decoded = DataModel::Decode(*reader, v); break; }
        case 4: { Clusters::Switch::Events::LongRelease::DecodableType v; decoded = DataModel::Decode(*reader, v); break; }
        case 5: { Clusters::Switch::Events::MultiPressOngoing::DecodableType v; decoded = DataModel::Decode(*reader, v); presses = v.currentNumberOfPressesCounted; break; }
        case 6: { Clusters::Switch::Events::MultiPressComplete::DecodableType v; decoded = DataModel::Decode(*reader, v); presses = v.totalNumberOfPressesCounted; break; }
        default: break;
        }
        if (decoded != CHIP_NO_ERROR) return;
        // Initial reports are history. Start a fresh baseline on every session
        // so a remote reboot (reset event counter) cannot disable its buttons.
        if (seen[button] && header.mEventNumber <= last[button]) return;
        seen[button] = true;
        last[button] = header.mEventNumber;
        if (!live) return;
        ESP_LOGI(TAG, "%s button ep=%u type=%lu count=%u event=%llu", names[slot], ep,
                 (unsigned long)eventId, presses, static_cast<unsigned long long>(header.mEventNumber));
        if (eventId == 1) {
            ++trace.buttons[slot - 1];
            cancelReadySignal();
            // MultiPressComplete distinguishes a click from double press/hold.
            if (!(switchFeatures[button] & 0x1c)) requestState(button == 0);
        } else if (eventId == 2) {
            cancelReadySignal(); desired = -1;
            lighting_hold(slot, button, true);
        } else if (eventId == 3 || eventId == 4) {
            lighting_hold(slot, button, false);
            if (eventId == 3 && !(switchFeatures[button] & 0x10)) requestState(button == 0);
        } else if (eventId == 6 && presses == 1) {
            requestState(button == 0);
        } else if (eventId == 6 && presses == 2) {
            requestState(true); // Make the selected colour visible, including from Off.
            if (!lighting_color(button == 0 ? 1 : -1)) ESP_LOGW(TAG, "Bulb colour state not ready");
        }
    }
    void OnSubscriptionEstablished(SubscriptionId id) override {
        live = true;
        ESP_LOGI(TAG, "%s listening (subscription=%lu)", names[slot], static_cast<unsigned long>(id));
    }
    void OnError(CHIP_ERROR error) override {
        if (slot > 0) { lighting_hold(slot, 0, false); lighting_hold(slot, 1, false); }
        discoveryFailed = true;
        live = false;
        ESP_LOGW(TAG, "%s interaction: %s", names[slot], ErrorStr(error));
    }
    void OnDone(ReadClient *) override {
        if (slot > 0) { lighting_hold(slot, 0, false); lighting_hold(slot, 1, false); }
        live = busy = false;
        finished = true;
        due = seconds() + 2;
    }
    void tick() {
        if (restart) {
            restart = false;
            client.reset();
            finished = busy = live = false;
            due = seconds();
        }
        if (busy || seconds() < due || !records[slot].paired || pairingSlot >= 0) return;
        client.reset();
        if (finished && discovery && !discoveryFailed) {
            std::sort(endpoints.begin(), endpoints.begin() + count);
            auto &r = records[slot];
            // Only choose automatically when the descriptor is unambiguous.
            if (slot == 0 && count == 1) {
                r.on = endpoints[0];
                save(slot);
            } else if (slot != 0 && count == 2) {
                r.on = endpoints[0]; r.off = endpoints[1];
                save(slot);
                ESP_LOGI(TAG, "%s endpoints %u/%u; use map if buttons are reversed", names[slot], r.on, r.off);
            } else {
                ESP_LOGW(TAG, "%s: %u matching endpoints; use map to select endpoints", names[slot], unsigned(count));
                due = seconds() + 30;
                finished = false;
                return;
            }
        }
        finished = false;
        if (slot == 0 && records[0].on != kInvalidEndpointId) return;
        begin(records[slot].on == kInvalidEndpointId || (slot != 0 && records[slot].off == kInvalidEndpointId));
    }
};
static Peer bulb(0), door(1), desk(2), bedside(3);
static Peer *peers[] = { &bulb, &door, &desk, &bedside };

static void commandResult(CHIP_ERROR err) {
    sending = false;
    if (err == CHIP_NO_ERROR) {
        ++trace.acknowledged;
        if (desired == sent) desired = -1;
        commandAttempts = 0;
        commandDue = seconds();
        ESP_LOGI(TAG, "Bulb acknowledged %s", sent ? "on" : "off");
        if (readySignal == ReadySignal::Flashing) {
            if (++readyStep == 4) {
                readySignal = ReadySignal::Done;
                ESP_LOGI(TAG, "READY: bulb acknowledged two startup flashes; all three remotes connected");
            } else {
                // Off -> On -> Off -> On, with a visible pause after each ACK.
                desired = readyStep % 2;
                commandDue = seconds() + 1;
                readyCommandDue = esp_timer_get_time() + 1000000;
            }
        }
    } else {
        ++trace.failed;
        ESP_LOGW(TAG, "Bulb command: %s", ErrorStr(err));
        // On/Off may safely be retried; Toggle must not be retried blindly.
        if (++commandAttempts >= 3) {
            desired = -1;
            ESP_LOGE(TAG, "Bulb unavailable after three attempts; press again to retry");
            if (readySignal == ReadySignal::Flashing) readySignal = ReadySignal::Failed;
        }
        commandDue = seconds() + 2;
    }
}
static void commandConnected(void *, Messaging::ExchangeManager &mgr, const SessionHandle &session) {
    auto ok = [](const ConcreteCommandPath &, const StatusIB &, const DataModel::NullObjectType &) {
        commandResult(CHIP_NO_ERROR);
    };
    auto fail = [](CHIP_ERROR error) { commandResult(error); };
    CHIP_ERROR err;
    if (sent)
        err = Controller::InvokeCommandRequest(&mgr, session, records[0].on, Clusters::OnOff::Commands::On::Type{}, ok, fail);
    else
        err = Controller::InvokeCommandRequest(&mgr, session, records[0].on, Clusters::OnOff::Commands::Off::Type{}, ok, fail);
    if (err != CHIP_NO_ERROR) commandResult(err);
}
static void commandFailed(void *, const ScopedNodeId &, CHIP_ERROR error) { commandResult(error); }
static Callback::Callback<OnDeviceConnected> commandConnection(commandConnected, nullptr);
static Callback::Callback<OnDeviceConnectionFailure> commandFailure(commandFailed, nullptr);

static void tick(intptr_t) {
    if (pairingSlot >= 0 && discoveryDeadline && seconds() >= discoveryDeadline) {
        // Upstream discovery timeout can occur before a PASE proxy exists and
        // then omit the pairing callback. Explicitly clear its discovery state.
        const int slot = pairingSlot;
        discoveryDeadline = 0;
        CHIP_ERROR err = commissioner().StopPairing(nodeFor(slot));
        if (err == CHIP_NO_ERROR) {
            pairingSlot = -1;
            ESP_LOGW(TAG, "%s not found in pairing mode; ready to retry", names[slot]);
        } else {
            ESP_LOGE(TAG, "Cannot stop discovery: %s; restart hub before retry", ErrorStr(err));
        }
    }
    for (auto *p : peers) p->tick();
    lighting_tick(records[0].paired ? records[0].on : kInvalidEndpointId,
                  pairingSlot < 0 && !sending && desired < 0);
    if (readySignal == ReadySignal::Waiting && pairingSlot < 0 && !sending && desired < 0 &&
        records[0].paired && records[0].on != kInvalidEndpointId && lighting_ready() &&
        door.live && desk.live && bedside.live) {
        readySignal = ReadySignal::Flashing;
        readyStep = 0;
        readyCommandDue = 0;
        desired = 0;
        commandAttempts = 0;
        commandDue = seconds();
        ESP_LOGI(TAG, "All three remotes connected; flashing bulb twice for startup readiness");
    }
    if (traceSample < sizeof(traceTimes) / sizeof(traceTimes[0]) && seconds() >= traceTimes[traceSample]) {
        ++traceSample;
        trace.paired = trace.listening = 0;
        for (int i = 0; i < 4; ++i) {
            if (records[i].paired) trace.paired |= 1u << i;
            if (peers[i]->live) trace.listening |= 1u << i;
        }
        esp_openthread_lock_acquire(portMAX_DELAY);
        auto *ot = esp_openthread_get_instance();
        trace.role = otThreadGetDeviceRole(ot);
        trace.srp = otSrpServerGetState(ot);
        esp_openthread_lock_release();
        saveTrace();
    }
    if (desired >= 0 && !sending && !lighting_busy() && seconds() >= commandDue && pairingSlot < 0 &&
        (readySignal != ReadySignal::Flashing || esp_timer_get_time() >= readyCommandDue)) {
        sending = true;
        sent = desired;
        CHIP_ERROR err = commissioner().GetConnectedDevice(nodeFor(0), &commandConnection, &commandFailure);
        if (err != CHIP_NO_ERROR) commandResult(err);
    }
}
static void timerCallback(void *) {
    CHIP_ERROR err = DeviceLayer::PlatformMgr().ScheduleWork(tick, 0);
    if (err != CHIP_NO_ERROR) ESP_LOGW(TAG, "Work queue busy: %s", ErrorStr(err));
}

static void paired(ScopedNodeId node) {
    discoveryDeadline = 0;
    int slot = pairingSlot;
    pairingSlot = -1;
    if (slot < 0 || node.GetNodeId() != nodeFor(slot)) { ESP_LOGE(TAG, "Unexpected commissioning result"); return; }
    records[slot] = Record{};
    records[slot].paired = 1;
    if (save(slot)) ESP_LOGI(TAG, "%s commissioned and persisted; discovering endpoints", names[slot]);
    peers[slot]->due = seconds() + 2;
}
static void pairingFailed(ScopedNodeId, CHIP_ERROR error, Controller::CommissioningStage,
                         std::optional<Credentials::AttestationVerificationResult>) {
    ESP_LOGE(TAG, "Commissioning failed: %s; correct cause then retry", ErrorStr(error));
    discoveryDeadline = 0;
    pairingSlot = -1;
}
static void paseResult(CHIP_ERROR error) {
    discoveryDeadline = 0;
    if (error != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Pairing session failed: %s; ready to retry", ErrorStr(error));
        pairingSlot = -1;
    }
}
static int findSlot(const char *name) {
    for (int i = 0; i < 4; ++i) if (!strcmp(name, names[i])) return i;
    return -1;
}
static void removed(NodeId node, CHIP_ERROR error) {
    int slot = pairingSlot;
    pairingSlot = -1;
    if (slot < 0 || node != nodeFor(slot)) return;
    if (error != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Remove %s failed: %s; saved pairing retained", names[slot], ErrorStr(error));
        peers[slot]->due = seconds() + 2;
        return;
    }
    records[slot] = Record{};
    save(slot);
    ESP_LOGI(TAG, "%s fabric removed; ready to pair again", names[slot]);
}
static bool endpointNumber(const char *text, EndpointId &out) {
    char *end;
    unsigned long value = strtoul(text, &end, 0);
    if (!*text || *end || value >= kInvalidEndpointId) return false;
    out = static_cast<EndpointId>(value);
    return true;
}
static esp_err_t console(int argc, char **argv) {
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    if (argc == 1 && !strcmp(argv[0], "history")) {
        BootTrace previous;
        size_t length = sizeof(previous);
        esp_err_t err = nvs_get_blob(store, "prevtrace", &previous, &length);
        if (err == ESP_OK && length == sizeof(previous) && previous.version == 1)
            printTrace("Previous startup", previous);
        else ESP_LOGI(TAG, "No previous startup trace");
        printTrace("Current startup", trace);
        return ESP_OK;
    }
    if (argc == 1 && !strcmp(argv[0], "status")) {
        ESP_LOGI(TAG, "Startup ready signal: %s", readySignalName());
        lighting_status();
        ESP_LOGI(TAG, "Heap free=%u largest=%u min=%u pairing=%s storage=%s",
                 unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                 pairingSlot < 0 ? "idle" : names[pairingSlot], storageHealthy ? "ok" : "ERROR");
        for (int i = 0; i < 4; ++i)
            ESP_LOGI(TAG, "%s node=0x%llx paired=%u onEP=%u offEP=%u listening=%u",
                     names[i], static_cast<unsigned long long>(nodeFor(i)), records[i].paired,
                     records[i].on, records[i].off, peers[i]->live);
        esp_openthread_lock_acquire(portMAX_DELAY);
        auto *ot = esp_openthread_get_instance();
        ESP_LOGI(TAG, "Thread role=%u SRP=%u", unsigned(otThreadGetDeviceRole(ot)), unsigned(otSrpServerGetState(ot)));
        esp_openthread_lock_release();
        return ESP_OK;
    }
    if (argc == 1 && (!strcmp(argv[0], "on") || !strcmp(argv[0], "off"))) {
        requestState(!strcmp(argv[0], "on")); return ESP_OK;
    }
    if (argc == 2 && !strcmp(argv[0], "level")) {
        char *end; unsigned long percent = strtoul(argv[1], &end, 10);
        if (!*argv[1] || *end || percent > 100) return ESP_ERR_INVALID_ARG;
        cancelReadySignal(); desired = -1;
        return lighting_level(percent) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (argc == 2 && !strcmp(argv[0], "color") && (!strcmp(argv[1], "next") || !strcmp(argv[1], "prev"))) {
        requestState(true);
        return lighting_color(!strcmp(argv[1], "next") ? 1 : -1) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (argc == 3 && !strcmp(argv[0], "pair")) {
        int slot = findSlot(argv[1]);
        if (slot < 0 || pairingSlot >= 0 || sending || lighting_busy() || !storageHealthy || records[slot].paired) return ESP_ERR_INVALID_STATE;
        otOperationalDatasetTlvs dataset;
        esp_openthread_lock_acquire(portMAX_DELAY);
        auto *ot = esp_openthread_get_instance();
        otDeviceRole role = otThreadGetDeviceRole(ot);
        otError err = otDatasetGetActiveTlvs(ot, &dataset);
        esp_openthread_lock_release();
        if (err != OT_ERROR_NONE || role < OT_DEVICE_ROLE_CHILD) return ESP_ERR_INVALID_STATE;
        pairingSlot = slot;
        discoveryDeadline = seconds() + 40;
        esp_err_t result = ctrl::pairing_code_thread(nodeFor(slot), argv[2], dataset.mTlvs, dataset.mLength);
        if (result != ESP_OK) { pairingSlot = -1; discoveryDeadline = 0; }
        return result;
    }
    if (argc == 2 && !strcmp(argv[0], "remove")) {
        int slot = findSlot(argv[1]);
        if (slot < 0 || pairingSlot >= 0 || sending || lighting_busy() || !records[slot].paired ||
            (peers[slot]->busy && !peers[slot]->live)) return ESP_ERR_INVALID_STATE;
        peers[slot]->client.reset();
        peers[slot]->busy = peers[slot]->live = peers[slot]->finished = false;
        pairingSlot = slot;
        esp_err_t result = ctrl::matter_controller_client::get_instance().unpair(nodeFor(slot), removed);
        if (result != ESP_OK) pairingSlot = -1;
        return result;
    }
    if ((argc == 3 || argc == 4) && !strcmp(argv[0], "map")) {
        int slot = findSlot(argv[1]);
        EndpointId on, off = kInvalidEndpointId;
        if (slot < 0 || !records[slot].paired || pairingSlot >= 0 || (peers[slot]->busy && !peers[slot]->live) ||
            !endpointNumber(argv[2], on) || (slot != 0 && (argc != 4 || !endpointNumber(argv[3], off) || off == on)))
            return ESP_ERR_INVALID_ARG;
        records[slot].on = on; records[slot].off = off;
        peers[slot]->live = false;
        peers[slot]->restart = true;
        return save(slot) ? ESP_OK : ESP_FAIL;
    }
    ESP_LOGI(TAG, "Commands: matter esp hub status | history | pair SLOT CODE | remove SLOT | on | off | level PERCENT | color next/prev | map SLOT ON_EP [OFF_EP]");
    return ESP_ERR_INVALID_ARG;
}

static void startThread() {
    esp_openthread_lock_acquire(portMAX_DELAY);
    auto *ot = esp_openthread_get_instance();
    otOperationalDatasetTlvs tlvs;
    otError err = otDatasetGetActiveTlvs(ot, &tlvs);
    if (err == OT_ERROR_NOT_FOUND) {
        otOperationalDataset dataset{};
        err = otDatasetCreateNewNetwork(ot, &dataset);
        if (err == OT_ERROR_NONE) {
            strcpy(dataset.mNetworkName.m8, "C6-Lab");
            err = otDatasetSetActive(ot, &dataset);
        }
    }
    if (err != OT_ERROR_NONE) { ESP_LOGE(TAG, "Thread dataset error %u", unsigned(err)); abort(); }
    otLinkModeConfig mode{};
    mode.mRxOnWhenIdle = mode.mDeviceType = mode.mNetworkData = true;
    if (otThreadSetLinkMode(ot, mode) != OT_ERROR_NONE ||
        otThreadSetRouterEligible(ot, true) != OT_ERROR_NONE ||
        otIp6SetEnabled(ot, true) != OT_ERROR_NONE || otThreadSetEnabled(ot, true) != OT_ERROR_NONE) abort();
    otSrpServerSetEnabled(ot, true);
    esp_openthread_lock_release();
}
static void installFabricIpk() {
    // The SDK example installs a public test IPK. Replace it before pairing
    // with a device-local random value, restoring exactly that value on boot.
    uint8_t ipk[Crypto::CHIP_CRYPTO_SYMMETRIC_KEY_LENGTH_BYTES];
    size_t length = sizeof(ipk);
    esp_err_t err = nvs_get_blob(store, "fabric_ipk", ipk, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (Crypto::DRBG_get_bytes(ipk, sizeof(ipk)) != CHIP_NO_ERROR) abort();
        ESP_ERROR_CHECK(nvs_set_blob(store, "fabric_ipk", ipk, sizeof(ipk)));
        ESP_ERROR_CHECK(nvs_commit(store));
    } else if (err != ESP_OK || length != sizeof(ipk)) {
        ESP_LOGE(TAG, "Corrupt fabric IPK; refusing replacement"); abort();
    }
    uint8_t compressed[8];
    MutableByteSpan span(compressed);
    if (commissioner().GetCompressedFabricIdBytes(span) != CHIP_NO_ERROR ||
        Credentials::SetSingleIpkEpochKey(Credentials::GetGroupDataProvider(), commissioner().GetFabricIndex(),
                                         ByteSpan(ipk), span) != CHIP_NO_ERROR) abort();
    set_issuer_ipk(ByteSpan(ipk));
    Crypto::ClearSecretData(ipk);
}
extern "C" void app_main() {
    consolePrinter = esp_log_set_vprintf(connectedLog);
    // Never erase identity automatically on NVS errors.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition("matter_nvs"));
    ESP_ERROR_CHECK(nvs_open_from_partition("matter_nvs", "hub", NVS_READWRITE, &store));
    BootTrace previous;
    size_t traceLength = sizeof(previous);
    if (nvs_get_blob(store, "boottrace", &previous, &traceLength) == ESP_OK &&
        traceLength == sizeof(previous) && previous.version == 1) {
        trace.boot = previous.boot + 1;
        ESP_ERROR_CHECK(nvs_set_blob(store, "prevtrace", &previous, sizeof(previous)));
    }
    trace.reset = esp_reset_reason();
    traceStage(1);
    for (int i = 0; i < 4; ++i) {
        size_t size = sizeof(Record);
        esp_err_t err = nvs_get_blob(store, names[i], &records[i], &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) records[i] = Record{};
        else if (err != ESP_OK || size != sizeof(Record) || records[i].version != 1) {
            ESP_LOGE(TAG, "Invalid saved %s record; refusing identity reset", names[i]); abort();
        }
    }
    install_persistent_issuer();
    static esp_openthread_platform_config_t platform{};
    platform.radio_config.radio_mode = RADIO_MODE_NATIVE;
    platform.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    platform.port_config.storage_partition_name = "nvs";
    platform.port_config.netif_queue_size = 10;
    platform.port_config.task_queue_size = 10;
    ESP_ERROR_CHECK(set_openthread_platform_config(&platform));
    ESP_ERROR_CHECK(esp_matter::start(nullptr));
    traceStage(2);
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        startThread();
        traceStage(3);
        auto &controller = ctrl::matter_controller_client::get_instance();
        ESP_ERROR_CHECK(controller.init(0xc600, 1, 5540));
        ESP_ERROR_CHECK(controller.setup_commissioner());
        installFabricIpk();
        lighting_init(store, [](bool ok) { if (ok) ++trace.acknowledged; else ++trace.failed; });
        ctrl::pairing_command_callbacks_t callbacks{};
        callbacks.pase_callback = paseResult;
        callbacks.commissioning_success_callback = paired;
        callbacks.commissioning_failure_callback = pairingFailed;
        ctrl::pairing_command::get_instance().set_callbacks(callbacks);
        traceStage(4);
    }
    static const esp_matter::console::command_t commands[] = {
        { "hub", "Local three-remote bulb controller", console }
    };
    ESP_ERROR_CHECK(esp_matter::console::add_commands(commands, 1));
    esp_timer_create_args_t timerArgs{};
    timerArgs.callback = timerCallback;
    timerArgs.name = "hub_retry";
    static esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));
    traceStage(5);
    ESP_LOGI(TAG, "Local hub started. Wait for Thread leader, then: matter esp hub status");
    // Hub operation starts before optional console initialization, including
    // terminal probing. Console setup must never gate subscriptions or control.
    auto startConsole = [](void *) {
        // Native USB console setup is useful only with an actual USB host.
        // A charger must never enter its driver/terminal initialization path.
        while (!usb_serial_jtag_is_connected()) vTaskDelay(pdMS_TO_TICKS(250));
        esp_err_t err = esp_matter::console::init();
        if (err != ESP_OK) ESP_LOGW(TAG, "Optional console unavailable: %s", esp_err_to_name(err));
        DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { traceStage(6); }, 0);
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(startConsole, "hub_console_init", 4096, nullptr, 1, nullptr) != pdPASS)
        ESP_LOGW(TAG, "Optional console initialization task unavailable");
}

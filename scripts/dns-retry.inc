// Bounded operational DNS retries for a Thread SRP service that has not yet
// registered. Stable slots remain reserved until an in-flight callback ends.
struct HubDnsQuery
{
    bool used = false, active = false;
    unsigned attempts = 0;
    PeerId peer;
    DnssdService service;
    DnssdResolveCallback callback = nullptr;
    void * context = nullptr;
};
static HubDnsQuery hubDnsQueries[6];
static void HubDnsRetry(System::Layer *, void *);
static void HubDnsResult(void * context, DnssdService * result,
                         const Span<Inet::IPAddress> & addresses, CHIP_ERROR error)
{
    auto * query = static_cast<HubDnsQuery *>(context);
    if (!query->active) { query->used = false; return; }
    if (error != CHIP_NO_ERROR && ++query->attempts < 20)
    {
        ChipLogProgress(Discovery, "Hub DNS awaiting SRP registration (attempt %u)", query->attempts);
        CHIP_ERROR timerError = DeviceLayer::SystemLayer().StartTimer(System::Clock::Seconds16(2), HubDnsRetry, query);
        if (timerError == CHIP_NO_ERROR) return;
        error = timerError;
    }
    // Copy state before invoking a callback which can synchronously cancel it.
    auto callback = query->callback;
    auto originalContext = query->context;
    DnssdService failedService = query->service;
    query->active = query->used = false;
    callback(originalContext, error == CHIP_NO_ERROR ? result : &failedService, addresses, error);
}
static void HubDnsRetry(System::Layer *, void * context)
{
    auto * query = static_cast<HubDnsQuery *>(context);
    if (!query->active) { query->used = false; return; }
    CHIP_ERROR error = ChipDnssdResolve(&query->service, Inet::InterfaceId::Null(), HubDnsResult, query);
    if (error != CHIP_NO_ERROR) HubDnsResult(query, nullptr, Span<Inet::IPAddress>(), error);
}
static CHIP_ERROR HubDnsStart(const PeerId & peer, const DnssdService & service,
                             DnssdResolveCallback callback, void * context)
{
    for (auto & query : hubDnsQueries)
        if (query.used && query.active && query.peer == peer) return CHIP_NO_ERROR;
    for (auto & query : hubDnsQueries)
    {
        if (query.used) continue;
        query.used = query.active = true;
        query.attempts = 0;
        query.peer = peer;
        query.service = service;
        query.callback = callback;
        query.context = context;
        CHIP_ERROR error = ChipDnssdResolve(&query.service, Inet::InterfaceId::Null(), HubDnsResult, &query);
        if (error != CHIP_NO_ERROR) query.used = query.active = false;
        return error;
    }
    return CHIP_ERROR_NO_MEMORY;
}
static void HubDnsCancel(const PeerId & peer)
{
    for (auto & query : hubDnsQueries)
    {
        if (!query.used || query.peer != peer) continue;
        // A pending timer means no DNS callback owns the slot.
        bool timerPending = DeviceLayer::SystemLayer().IsTimerActive(HubDnsRetry, &query);
        DeviceLayer::SystemLayer().CancelTimer(HubDnsRetry, &query);
        query.active = false;
        if (timerPending) query.used = false;
    }
}

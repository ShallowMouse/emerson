#include "RouteStatus.h"

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include "Route/RouteShared.h"
 #include <cstring>
#endif

namespace emerson
{

RouteStatus::~RouteStatus() { close(); }

void RouteStatus::close()
{
   #if JUCE_WINDOWS
    if (view != nullptr)        UnmapViewOfFile (view);
    if (mapping != nullptr)     CloseHandle ((HANDLE) mapping);
    if (infoView != nullptr)    UnmapViewOfFile (infoView);
    if (infoMapping != nullptr) CloseHandle ((HANDLE) infoMapping);
   #endif
    view = nullptr;
    mapping = nullptr;
    infoView = nullptr;
    infoMapping = nullptr;
}

RouteStatus::Info RouteStatus::poll()
{
    Info info;
   #if JUCE_WINDOWS
    using namespace hawthorne::route;
    if (view == nullptr)
    {
        // Hawthorne creates the mapping; until it (or a program using the driver) has, there's nothing to read
        mapping = OpenFileMappingW (FILE_MAP_READ, FALSE, mappingName);
        if (mapping == nullptr) return info;
        // read-only; only the header is ever looked at
        view = MapViewOfFile ((HANDLE) mapping, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr) { close(); return info; }
    }
    const auto* s = static_cast<const Shared*> (view);
    if (s->magic != magic || s->version != version) return info;
    info.sampleRate = s->sampleRate.load();
    const auto beat = s->heartbeatMs.load();
    info.hawthorneRunning = info.sampleRate > 0 && (std::int64_t) GetTickCount64() - beat < 500;
    info.tracks = info.hawthorneRunning ? tracksOf (s->numTracks.load()) : 0;
    if (info.hawthorneRunning)
        readInfo (info);
   #endif
    return info;
}

void RouteStatus::readInfo (Info& info)
{
   #if JUCE_WINDOWS
    using namespace hawthorne::route;
    if (infoView == nullptr)
    {
        // an older Hawthorne doesn't make it: then the strips just keep their numbers
        infoMapping = OpenFileMappingW (FILE_MAP_READ, FALSE, infoMappingName);
        if (infoMapping == nullptr) return;
        infoView = MapViewOfFile ((HANDLE) infoMapping, FILE_MAP_READ, 0, 0, 0);
        if (infoView == nullptr) { CloseHandle ((HANDLE) infoMapping); infoMapping = nullptr; return; }
    }
    const auto* src = static_cast<const hawthorne::route::Info*> (infoView);
    if (src->magic != infoMagic || src->version != infoVersion) return;

    // copy it out while Hawthorne isn't writing (the sequence number is even and unchanged)
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto before = src->seq.load (std::memory_order_acquire);
        if ((before & 1u) != 0) { juce::Thread::yield(); continue; }
        Info got;
        const int n = juce::jlimit (0, maxTracks, (int) src->numTracks);
        char buf[infoNameBytes + 1] = {};
        char reel[sizeof (src->reelName) + 1] = {};
        std::memcpy (reel, src->reelName, sizeof (src->reelName));
        got.reelName = juce::String::fromUTF8 (reel).trim();
        for (int t = 0; t < n; ++t)
        {
            std::memcpy (buf, src->trackNames[t], infoNameBytes);
            buf[infoNameBytes] = 0;
            got.trackNames.add (juce::String::fromUTF8 (buf).trim());
            got.armed.push_back (src->armed[t] != 0);
            got.toMix.push_back (src->toMix[t] != 0);
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (src->seq.load (std::memory_order_acquire) != before) continue;
        info.hasInfo = n > 0;
        info.reelName = got.reelName;
        info.trackNames = got.trackNames;
        info.armed = got.armed;
        info.toMix = got.toMix;
        return;
    }
   #else
    juce::ignoreUnused (info);
   #endif
}

} // namespace emerson

#include "LinkHost.h"
#include <chrono>
#include <cstring>

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace emerson
{

std::int64_t LinkHost::nowMs() noexcept
{
    using namespace std::chrono;
    return duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

LinkHost::~LinkHost() { close(); }

bool LinkHost::open()
{
    close();
#ifdef _WIN32
    const auto size = (DWORD) sizeof (link::Shared);
    auto* m = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, link::mappingName);
    if (m == nullptr) return false;
    const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    auto* view = MapViewOfFile (m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (link::Shared));
    if (view == nullptr) { CloseHandle (m); return false; }
    mapping = m;
    shared = static_cast<link::Shared*> (view);
    tick = CreateEventW (nullptr, FALSE, FALSE, link::tickName);   // auto-reset; WHITMAN waits on it

    if (! existed || shared->magic != link::magic || shared->version != link::version)
    {
        // fresh (a mapping is zeroed when it's made): just stamp it
        shared->version = link::version;
        shared->magic = link::magic;
    }
    shared->sampleRate = 0;
    return true;
#else
    return false;
#endif
}

void LinkHost::attach (link::Shared* memory)
{
    close();
    shared = memory;
    shared->magic = link::magic;
    shared->version = link::version;
}

void LinkHost::close()
{
    if (shared != nullptr) shared->sampleRate = 0;
#ifdef _WIN32
    if (mapping != nullptr)
    {
        UnmapViewOfFile (shared);
        CloseHandle ((HANDLE) mapping);
    }
    if (tick != nullptr) CloseHandle ((HANDLE) tick);
#endif
    mapping = tick = nullptr;
    shared = nullptr;
}

void LinkHost::start (int sampleRate, int blockSize, int numTracks) noexcept
{
    if (shared == nullptr) return;
    shared->blockSize = juce::jlimit (1, link::maxBlock, blockSize);
    shared->numTracks = numTracks;
    shared->heartbeatMs = nowMs();
    shared->sampleRate = sampleRate;     // last: WHITMAN starts following once this is set
    missed = 0;
}

void LinkHost::stop() noexcept
{
    if (shared != nullptr) shared->sampleRate = 0;
}

void LinkHost::readFromWhitman (float* const* out, int n) noexcept
{
    const auto silence = [&]
    {
        for (int c = 0; c < link::fromWhitmanChannels; ++c)
            if (out[c] != nullptr) std::memset (out[c], 0, sizeof (float) * (size_t) n);
    };
    if (shared == nullptr || n <= 0) { if (n > 0) silence(); return; }

    // WHITMAN's answer to the block EMERSON wrote last time: [w - n, w)
    const auto w = shared->toWrite.load (std::memory_order_acquire);
    const auto from = w - n;
    if (from < 0 || shared->clientRunning.load (std::memory_order_relaxed) == 0)
    {
        silence();
        return;
    }
    if (shared->fromWrite.load (std::memory_order_acquire) < w)
    {
        // WHITMAN didn't get to it in time
        missed.fetch_add (1, std::memory_order_relaxed);
        silence();
        return;
    }

    for (int c = 0; c < link::fromWhitmanChannels; ++c)
    {
        if (out[c] == nullptr) continue;
        const auto* ring = shared->fromWhitman[c];
        int p = (int) (from & link::ringMask);
        const int first = juce::jmin (n, link::ringFrames - p);
        std::memcpy (out[c], ring + p, sizeof (float) * (size_t) first);
        if (first < n) std::memcpy (out[c] + first, ring, sizeof (float) * (size_t) (n - first));
    }
    lastGoodMs.store (nowMs(), std::memory_order_relaxed);
}

void LinkHost::writeToWhitman (const float* const* in, int numTracks, int n) noexcept
{
    if (shared == nullptr || n <= 0) return;
    const auto w = shared->toWrite.load (std::memory_order_relaxed);
    const int p = (int) (w & link::ringMask);
    const int first = juce::jmin (n, link::ringFrames - p);

    for (int c = 0; c < link::toWhitmanChannels; ++c)
    {
        auto* ring = shared->toWhitman[c];
        const bool used = c >= link::maxTracks || c < numTracks;
        const float* x = used ? in[c] : nullptr;
        if (x != nullptr)
        {
            std::memcpy (ring + p, x, sizeof (float) * (size_t) first);
            if (first < n) std::memcpy (ring, x + first, sizeof (float) * (size_t) (n - first));
        }
        else
        {
            std::memset (ring + p, 0, sizeof (float) * (size_t) first);
            if (first < n) std::memset (ring, 0, sizeof (float) * (size_t) (n - first));
        }
    }

    shared->heartbeatMs.store (nowMs(), std::memory_order_relaxed);
    shared->toWrite.store (w + n, std::memory_order_release);
#ifdef _WIN32
    if (tick != nullptr) SetEvent ((HANDLE) tick);
#endif
}

bool LinkHost::isWhitmanConnected() const noexcept
{
    return shared != nullptr && shared->clientRunning.load() != 0 && nowMs() - lastGoodMs.load() < 500;
}

//==============================================================================
DirectOutPatch::~DirectOutPatch()
{
#ifdef _WIN32
    if (mapping != nullptr)
    {
        UnmapViewOfFile (patch);
        CloseHandle ((HANDLE) mapping);
    }
#endif
}

bool DirectOutPatch::open()
{
#ifdef _WIN32
    auto* m = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (directout::Patch), directout::mappingName);
    if (m == nullptr) return false;
    auto* view = MapViewOfFile (m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (directout::Patch));
    if (view == nullptr) { CloseHandle (m); return false; }
    mapping = m;
    patch = static_cast<directout::Patch*> (view);
    patch->version = directout::version;
    patch->magic = directout::magic;
    return true;
#else
    return false;
#endif
}

void DirectOutPatch::attach (directout::Patch* memory)
{
    patch = memory;
    patch->version = directout::version;
    patch->magic = directout::magic;
}

void DirectOutPatch::publish (bool enabled, int numTracks, const int* outputs) noexcept
{
    if (patch == nullptr) return;
    auto s = patch->seq.load();
    if (s & 1u) ++s;
    patch->seq.store (s + 1u);                                     // odd: writing
    patch->enabled = enabled ? 1 : 0;
    patch->numTracks = juce::jlimit (0, directout::maxTracks, numTracks);
    for (int t = 0; t < directout::maxTracks; ++t)
        patch->output[t] = (std::int16_t) (t < numTracks && outputs != nullptr ? juce::jlimit (-1, 1023, outputs[t]) : -1);
    patch->seq.store (s + 2u);                                     // even again: done
}

} // namespace emerson

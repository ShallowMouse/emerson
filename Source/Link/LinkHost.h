#pragma once

#include "EmersonLink.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace emerson
{

//==============================================================================
/** EMERSON's end of the EMERSON Link (EmersonLink.h): makes the shared memory, writes
    the tracks and returns into it every block and wakes WHITMAN, and reads back what
    WHITMAN wrote for the block before.

    The exchange itself works on any Shared block, so the tests run it in one process
    with WHITMAN's side (LinkClientCore) on the same memory. open() makes the real,
    named mapping (Windows). */
class LinkHost
{
public:
    LinkHost() = default;
    ~LinkHost();
    LinkHost (const LinkHost&) = delete;

    /** Makes the named mapping and event (Windows). False if that failed. */
    bool open();
    /** Uses this memory instead (tests). */
    void attach (link::Shared* memory);
    void close();
    bool isOpen() const noexcept { return shared != nullptr; }

    /** Audio starting / stopping. */
    void start (int sampleRate, int blockSize, int numTracks) noexcept;
    void stop() noexcept;
    void setNumTracks (int n) noexcept { if (shared != nullptr) shared->numTracks = n; }

    /** Audio thread, at the start of a block: what WHITMAN made of the last block
        (its mix and sends; silence if it isn't there or fell behind).
        out: [fromWhitmanChannels] pointers, each numSamples long. */
    void readFromWhitman (float* const* out, int numSamples) noexcept;

    /** Audio thread, after the rack: this block's tracks and returns, then wake WHITMAN.
        in: [toWhitmanChannels] pointers (nullptr = silence). */
    void writeToWhitman (const float* const* in, int numTracks, int numSamples) noexcept;

    /** WHITMAN has the link open and kept up (message thread, for the top bar). */
    bool isWhitmanConnected() const noexcept;
    /** WHITMAN missed blocks since start (glitches). */
    int dropouts() const noexcept { return missed.load(); }

    static std::int64_t nowMs() noexcept;

private:
    link::Shared* shared = nullptr;
    bool owned = false;
    void* mapping = nullptr;
    void* tick = nullptr;
    std::atomic<int> missed { 0 };
    std::atomic<std::int64_t> lastGoodMs { 0 };
};

//==============================================================================
/** The direct-out patch EMERSON asks Hawthorne for (EmersonLink.h, emerson::directout). */
class DirectOutPatch
{
public:
    ~DirectOutPatch();
    bool open();                   // the named mapping (Windows)
    void attach (directout::Patch* memory);
    void publish (bool enabled, int numTracks, const int* outputs) noexcept;   // outputs: per track, -1 = none

private:
    directout::Patch* patch = nullptr;
    void* mapping = nullptr;
};

} // namespace emerson

#pragma once

// EMERSON Link: how EMERSON (the FX rack) and WHITMAN (the mixer) pass audio.
//
//   Hawthorne --Hawthorne Route--> EMERSON --EMERSON Link--> WHITMAN
//   Hawthorne <--Hawthorne Route-- EMERSON <--EMERSON Link-- WHITMAN (the mix)
//
// EMERSON is the only program on Hawthorne Route. It writes every processed track, and
// what its FX returns made of WHITMAN's sends, into shared memory and signals an event.
// WHITMAN's "EMERSON Link" audio device (inside WHITMAN, no driver to install) wakes on
// the event, runs WHITMAN's mixer on the block and writes the mix and the sends back.
// EMERSON reads them one block later, so all three run off Hawthorne's clock:
// sample-locked, no drift, no resampling, a fixed one-block delay for the mix.
//
// The block size is EMERSON's (WHITMAN's device runs at whatever EMERSON writes).
// The channel names WHITMAN's device shows are the same as Hawthorne Route's
// ("Hawthorne track 7", "To Hawthorne master L"), so WHITMAN's routing doesn't change.
//
// This header is shared by EMERSON (Source/Link) and WHITMAN (Source/EmersonLink):
// keep the two copies identical. Plain C++.

#include <atomic>
#include <cstdint>

#ifndef SHALLOWMOUSE_LINK_SCOPE
// Inside MELVILLE (MELVILLE_HOSTED) every name gets MELVILLE's own prefix, so a standalone
// copy of the program running at the same time keeps to its own memory.
#if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
 #define SHALLOWMOUSE_LINK_SCOPE L"Melville."
#else
 #define SHALLOWMOUSE_LINK_SCOPE L""
#endif
#endif

namespace emerson::link
{
    constexpr std::uint32_t magic   = 0x454D4C31;   // "EML1"
    constexpr std::uint32_t version = 1;
    constexpr int maxTracks = 96;                   // as Hawthorne Route
    constexpr int numReturns = 2;                   // FX returns A and B (stereo)

    // EMERSON -> WHITMAN: slots 0-95 tracks, then return A L/R, return B L/R
    constexpr int toReturnA = maxTracks, toReturnB = maxTracks + 2;
    constexpr int toWhitmanChannels = maxTracks + 2 * numReturns;
    // WHITMAN -> EMERSON: mix L/R, send A L/R, send B L/R
    constexpr int fromMixLeft = 0, fromMixRight = 1, fromSendA = 2, fromSendB = 4;
    constexpr int fromWhitmanChannels = 2 + 2 * numReturns;

    constexpr int ringFrames = 1 << 14;             // 16384 frames per channel (power of two)
    constexpr int ringMask   = ringFrames - 1;
    constexpr int maxBlock   = 4096;

    constexpr const wchar_t* mappingName = L"Local\\" SHALLOWMOUSE_LINK_SCOPE L"EmersonLinkV1";
    constexpr const wchar_t* tickName    = L"Local\\" SHALLOWMOUSE_LINK_SCOPE L"EmersonLinkTickV1";

    struct Shared
    {
        std::uint32_t magic, version;
        // EMERSON side
        std::atomic<std::int32_t> sampleRate;       // 0 = EMERSON's audio isn't running
        std::atomic<std::int32_t> blockSize;        // EMERSON's buffer size (WHITMAN's device uses it)
        std::atomic<std::int32_t> numTracks;        // the reel's track count
        std::atomic<std::int64_t> toWrite;          // frames EMERSON has written into toWhitman
        std::atomic<std::int64_t> heartbeatMs;      // EMERSON's last callback (ms, monotonic)
        // WHITMAN side
        std::atomic<std::int32_t> clientRunning;    // WHITMAN has the device open and running
        std::atomic<std::int64_t> fromWrite;        // frames WHITMAN has written into fromWhitman
        std::atomic<std::int64_t> clientId;         // changes when WHITMAN (re)starts the device
        char clientName[64];

        float toWhitman[toWhitmanChannels][ringFrames];
        float fromWhitman[fromWhitmanChannels][ringFrames];
    };

    static_assert (std::atomic<std::int64_t>::is_always_lock_free, "shared-memory atomics must be lock-free");

    // Channel names of WHITMAN's "EMERSON" device
    //   inputs:  "Hawthorne track 1".."Hawthorne track N", "EMERSON return A L/R", "EMERSON return B L/R"
    //   outputs: "To Hawthorne master L/R", "To EMERSON send A L/R", "To EMERSON send B L/R"
}

//==============================================================================
// EMERSON's direct outs, for a hardware desk.
//
// With OUTPUT set to DIRECT OUTS, EMERSON plays each processed track back into Hawthorne
// on Hawthorne Route's "To Hawthorne track N" channels, and asks Hawthorne (here) to send
// each one straight out of an interface output. Hawthorne owns the interface, so the
// tracks stay sample-locked to the tape. EMERSON writes from its message thread when the
// patch changes; Hawthorne copies it out under the sequence number (odd while writing).
namespace emerson::directout
{
    constexpr std::uint32_t magic   = 0x454D4431;   // "EMD1"
    constexpr std::uint32_t version = 1;
    constexpr int maxTracks = 96;
    constexpr const wchar_t* mappingName = L"Local\\" SHALLOWMOUSE_LINK_SCOPE L"EmersonDirectOutV1";

    struct Patch
    {
        std::uint32_t magic, version;
        std::atomic<std::uint32_t> seq;
        std::int32_t  enabled;                      // 1 = EMERSON's OUTPUT is DIRECT OUTS
        std::int32_t  numTracks;
        std::int16_t  output[maxTracks];            // per track: Hawthorne's device output (0-based), -1 = none
    };
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free, "shared-memory atomics must be lock-free");
}

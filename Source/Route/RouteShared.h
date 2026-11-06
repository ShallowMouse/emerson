#pragma once

// Hawthorne Route: a virtual ASIO driver ("Hawthorne Route") that other programs
// - Reaper, a synth, anything with ASIO - can pick as their audio device. What they
// play out of it arrives in Hawthorne as ROUTE inputs (one per track, plus master L / R);
// back on the driver's inputs they get every Hawthorne track (what each track's meter
// shows), the master and LTC of the tape position. The track count follows the reel.
//
// The two sides meet in shared memory. Hawthorne's audio callback is the clock: it
// advances a frame counter and signals an event, and the driver runs the other
// program's ASIO callback from that, so both run sample-synchronously - no drift,
// no resampling. When Hawthorne isn't running, the driver keeps time by itself
// (silence) so the other program doesn't stall.
//
// Shared by Hawthorne (Source/Engine) and the driver (Source/RouteDriver). Plain C++.

#include <atomic>
#include <cstdint>

namespace hawthorne::route
{
    // v3: the channels follow the reel's track count (numTracks); the other program gets a
    // reset request when it changes.
    //   driver outputs (into Hawthorne):  tracks 1..N, master L, master R
    //   driver inputs (from Hawthorne):   tracks 1..N, master L, master R, LTC
    // In shared memory every channel has a fixed slot, whatever N is.
    constexpr std::uint32_t magic   = 0x48525433;   // "HRT3"
    constexpr std::uint32_t version = 3;
    constexpr int maxTracks = 96;
    // to Hawthorne: slots 0-95 track inputs, 96 / 97 master inputs
    constexpr int toMasterLeft = maxTracks, toMasterRight = maxTracks + 1;
    constexpr int toHawthorneChannels = maxTracks + 2;
    // from Hawthorne: slots 0-95 tracks, 96 / 97 master, 98 LTC
    constexpr int trackChannels = maxTracks;
    constexpr int masterLeft = maxTracks, masterRight = maxTracks + 1, ltcChannel = maxTracks + 2;
    constexpr int fromHawthorneChannels = maxTracks + 3;
    constexpr int defaultTracks = 24;
    inline int tracksOf (int n) noexcept { return n >= 1 && n <= maxTracks ? n : defaultTracks; }
    constexpr int ringFrames = 1 << 15;             // 32768 frames per channel (power of two)
    constexpr int ringMask   = ringFrames - 1;

    constexpr const wchar_t* mappingName = L"Local\\HawthorneRouteV3";
    constexpr const wchar_t* tickName    = L"Local\\HawthorneRouteTickV3";

    struct Shared
    {
        std::uint32_t magic, version;
        // Hawthorne side
        std::atomic<std::int32_t> sampleRate;        // Hawthorne's device rate (0 = Hawthorne not running)
        std::atomic<std::int64_t> clock;             // frames Hawthorne's device has run (drives the driver)
        std::atomic<std::int64_t> heartbeatMs;       // Hawthorne's last callback (ms, monotonic) - to tell if it's alive
        std::atomic<std::int64_t> fromWrite;         // frames Hawthorne has written into fromHawthorne
        std::atomic<std::int32_t> numTracks;         // the reel's track count (the channels the driver shows)
        // driver side
        std::atomic<std::int32_t> driverRunning;     // a program has the driver started
        std::atomic<std::int32_t> driverBlock;       // its buffer size
        std::atomic<std::int64_t> toWrite;           // frames the driver has written into toHawthorne
        std::atomic<std::int64_t> driverClientId;    // changes when a program (re)starts the driver
        char clientName[64];                         // the program's name, for Hawthorne's routing matrix

        float toHawthorne[toHawthorneChannels][ringFrames];
        float fromHawthorne[fromHawthorneChannels][ringFrames];
    };

    static_assert (std::atomic<std::int64_t>::is_always_lock_free, "shared-memory atomics must be lock-free");

    //--------------------------------------------------------------------------
    // Reel and track info, for programs on the other end that want to show it (WHITMAN's
    // strips take their names from it). Its own small mapping, so the audio block above -
    // and every driver already installed - stays exactly as it was. Hawthorne writes it from
    // its message thread a few times a second when something changes; readers copy it out
    // under the sequence number (odd while Hawthorne is writing; read again if it changed).
    constexpr std::uint32_t infoMagic   = 0x48524931;   // "HRI1"
    constexpr std::uint32_t infoVersion = 1;
    constexpr int infoNameBytes = 32;                   // UTF-8, nul-terminated
    constexpr const wchar_t* infoMappingName = L"Local\\HawthorneRouteInfoV1";

    struct Info
    {
        std::uint32_t magic, version;
        std::atomic<std::uint32_t> seq;
        std::int32_t  numTracks;                         // the reel's tracks (0 = no reel loaded)
        char reelName[64];                               // UTF-8
        char trackNames[maxTracks][infoNameBytes];       // UTF-8, empty = not named
        std::uint8_t armed[maxTracks];                   // 1 = armed to record
        std::uint8_t toMix[maxTracks];                   // 1 = the track feeds Hawthorne's own master mix
    };
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free, "shared-memory atomics must be lock-free");

    /** Absolute input ids for Hawthorne's routing (beside device inputs and the network's 1000+). */
    constexpr int inputBase = 3000;
    inline bool isRouteInput (int id) noexcept { return id >= inputBase && id < inputBase + toHawthorneChannels; }
    inline int  channelOf (int id) noexcept    { return id - inputBase; }
}

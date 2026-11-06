#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

namespace emerson
{

//==============================================================================
/** A delay of a whole number of samples (plugin latency compensation). */
class SampleDelay
{
public:
    void setDelay (int samples, int channels)
    {
        delay = juce::jmax (0, samples);
        buf.setSize (juce::jmax (1, channels), juce::jmax (1, delay));   // the swap below delays by exactly the buffer length
        buf.clear();
        pos = 0;
    }
    int getDelay() const noexcept { return delay; }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (delay <= 0) return;
        const int size = buf.getNumSamples();
        numCh = juce::jmin (numCh, buf.getNumChannels());
        for (int c = 0; c < numCh; ++c)
        {
            auto* d = buf.getWritePointer (c);
            auto* x = ch[c];
            int p = pos;
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                x[i] = d[p];
                d[p] = in;
                if (++p == size) p = 0;
            }
        }
        pos = (pos + n) % size;
    }

private:
    int delay = 0, pos = 0;
    juce::AudioBuffer<float> buf;
};

//==============================================================================
/** One effect in a chain. The UI owns it; the audio thread only reads. */
struct Slot
{
    int uid = 0;
    juce::String identifier;                 // what to load it from ("pequod:<id>")
    juce::String name, collection;
    std::unique_ptr<juce::AudioProcessor> processor;
    std::atomic<bool> bypassed { false };
    int preparedLatency = 0;                 // latency when the render was built
};

/** Where a chain sits. */
enum class ChainKind { track, fxReturn, mix };

//==============================================================================
/** An insert chain: a track's (mono), an FX return's or the mix's (stereo). */
struct Chain
{
    ChainKind kind = ChainKind::track;
    int index = 0;                                       // track number - 1, or return 0 / 1
    std::vector<std::unique_ptr<Slot>> slots;
    std::atomic<bool> bypassAll { false };
    std::atomic<float> trimDb { 0.0f };                  // input trim (tracks: before the chain)
    std::atomic<float> levelDb { 0.0f };                 // after the chain (returns, mix)

    // meters (peak, decaying; the UI reads them)
    std::atomic<float> peakIn { 0.0f }, peakOut { 0.0f };

    int numChannels() const noexcept { return kind == ChainKind::track ? 1 : 2; }
    bool isEmpty() const noexcept    { return slots.empty(); }
    int latency() const noexcept;                        // the chain's total, as last prepared
};

//==============================================================================
/** EMERSON's audio side: every track's insert chain, the two FX returns and the mix chain.

      tracks in (from Hawthorne) -> trim -> inserts -> latency alignment -> tracks out
      sends A / B (from WHITMAN)  -> return A / B inserts -> level -> returns (to WHITMAN)
      mix (from WHITMAN)          -> mix inserts -> level -> to Hawthorne's master

    Every track is delayed to the latest track chain, so the tracks stay in time with
    each other whatever plugins are on them (the delay shows in the top bar).

    Editing (message thread): change the chains, then call rebuild(). The audio thread
    picks the new render up at its next block; the old one is freed once it's done with
    it, along with any plugins that were removed. */
class Rack
{
public:
    static constexpr int maxTracks = 96;
    static constexpr int numReturns = 2;

    Rack();
    ~Rack();

    // ---- message thread
    Chain& track (int i)        { return *tracks[(size_t) i]; }
    Chain& fxReturn (int i)     { return *returns[(size_t) i]; }
    Chain& mix()                { return *mixChain; }
    const Chain& mix() const    { return *mixChain; }
    Chain& chain (ChainKind, int index);

    /** Sets the rate and block the plugins run at (prepares every plugin again). */
    void prepare (double sampleRate, int maxBlock);
    double getSampleRate() const { return sampleRate; }
    int getMaxBlock() const      { return maxBlock; }

    /** Prepares a new plugin for the chain it's going into (call before adding it). */
    void prepareSlot (Slot&, const Chain&) const;

    /** Publishes the chains as they are now to the audio thread. Removed slots can be
        handed over so they're freed only when the audio thread no longer uses them. */
    void rebuild (std::vector<std::unique_ptr<Slot>> removed = {});

    /** Frees old renders (call from a timer). Also rebuilds if a plugin's latency changed. */
    void housekeeping();

    /** The delay every track gets (the latest track chain), in samples. */
    int trackLatency() const noexcept { return currentTrackLatency.load(); }

    int nextUid() { return ++uidCounter; }

    // ---- audio thread
    struct IO
    {
        int numTracks = 0;                       // tracks in use (the reel's)
        const float* const* trackIn = nullptr;   // [numTracks], nullptr entries = silence
        float* const* trackOut = nullptr;        // [numTracks], nullptr entries = not wanted
        const float* sends[numReturns][2] {};    // WHITMAN's sends (nullptr = silence)
        float* returnsOut[numReturns][2] {};     // to WHITMAN
        const float* mixIn[2] {};                // WHITMAN's mix
        float* mixOut[2] {};                     // to Hawthorne
        int numSamples = 0;
    };
    void process (const IO&) noexcept;

private:
    struct Render;
    std::vector<std::unique_ptr<Chain>> tracks, returns;
    std::unique_ptr<Chain> mixChain;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    int uidCounter = 0;

    std::atomic<Render*> current { nullptr };
    std::atomic<Render*> inUse { nullptr };
    std::vector<std::unique_ptr<Render>> retired;
    std::atomic<int> currentTrackLatency { 0 };

    void freeRetired();
    std::unique_ptr<Render> build();
};

} // namespace emerson

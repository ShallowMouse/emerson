#include "Rack.h"

namespace emerson
{

int Chain::latency() const noexcept
{
    int l = 0;
    for (auto& s : slots) l += s->preparedLatency;
    return l;
}

//==============================================================================
namespace
{
    struct RSlot
    {
        Slot* slot = nullptr;
        juce::AudioProcessor* p = nullptr;
        int channels = 2;            // what the plugin runs with
        int latency = 0;
        SampleDelay bypassDelay;     // keeps the chain's delay the same when it's bypassed
    };

    struct RChain
    {
        Chain* chain = nullptr;
        std::vector<RSlot> slots;
        int latency = 0;
        SampleDelay align;           // tracks: up to the latest track chain
        juce::AudioBuffer<float> buf;
        juce::MidiBuffer midi;
        float trimCur = 1.0f, levelCur = 1.0f;
    };

    inline float dbToGain (float db) noexcept { return db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }

    inline float peakOf (const float* x, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i) p = juce::jmax (p, std::abs (x[i]));
        return p;
    }

    inline void meter (std::atomic<float>& m, float peak) noexcept
    {
        m.store (juce::jmax (peak, m.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    }

    /** Ramps a gain from cur to target across the block. */
    inline void applyGain (float* const* ch, int numCh, int n, float& cur, float target) noexcept
    {
        if (cur == target)
        {
            if (target != 1.0f)
                for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::multiply (ch[c], target, n);
            return;
        }
        const float step = (target - cur) / (float) juce::jmax (1, n);
        for (int c = 0; c < numCh; ++c)
        {
            float g = cur;
            for (int i = 0; i < n; ++i) { g += step; ch[c][i] *= g; }
        }
        cur = target;
    }

    /** Runs the chain on buf (channels: 1 for a track, 2 otherwise). */
    void runChain (RChain& rc, int numCh, int n) noexcept
    {
        auto* const* ch = rc.buf.getArrayOfWritePointers();
        const bool all = rc.chain->bypassAll.load (std::memory_order_relaxed);
        for (auto& s : rc.slots)
        {
            if (all || s.slot->bypassed.load (std::memory_order_relaxed))
            {
                s.bypassDelay.process (ch, numCh, n);
                continue;
            }
            // a stereo-only plugin on a mono track: feed it both sides, take the average back
            if (numCh == 1 && s.channels == 2)
                juce::FloatVectorOperations::copy (ch[1], ch[0], n);

            juce::AudioBuffer<float> view (ch, juce::jmax (numCh, s.channels), n);
            rc.midi.clear();
            s.p->processBlock (view, rc.midi);

            if (numCh == 1 && s.channels == 2)
            {
                juce::FloatVectorOperations::add (ch[0], ch[1], n);
                juce::FloatVectorOperations::multiply (ch[0], 0.5f, n);
            }
        }
    }
}

struct Rack::Render
{
    std::vector<RChain> tracks;
    RChain returns[numReturns];
    RChain mix;
    int trackLatency = 0;
    std::vector<std::unique_ptr<Slot>> graveyard;   // removed plugins: freed with this render
};

//==============================================================================
Rack::Rack()
{
    for (int i = 0; i < maxTracks; ++i)
    {
        tracks.push_back (std::make_unique<Chain>());
        tracks.back()->kind = ChainKind::track;
        tracks.back()->index = i;
    }
    for (int i = 0; i < numReturns; ++i)
    {
        returns.push_back (std::make_unique<Chain>());
        returns.back()->kind = ChainKind::fxReturn;
        returns.back()->index = i;
    }
    mixChain = std::make_unique<Chain>();
    mixChain->kind = ChainKind::mix;
    rebuild();
}

Rack::~Rack()
{
    // the audio must be stopped by now
    delete current.exchange (nullptr);
    retired.clear();
}

Chain& Rack::chain (ChainKind k, int index)
{
    switch (k)
    {
        case ChainKind::track:    return track (juce::jlimit (0, maxTracks - 1, index));
        case ChainKind::fxReturn: return fxReturn (juce::jlimit (0, numReturns - 1, index));
        case ChainKind::mix:      break;
    }
    return mix();
}

void Rack::prepareSlot (Slot& s, const Chain& c) const
{
    if (s.processor == nullptr) return;
    auto& p = *s.processor;
    p.releaseResources();

    // tracks are mono; if the plugin can't do mono it runs in stereo (see runChain)
    auto want = c.numChannels() == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (want);
    l.outputBuses.add (want);
    if (! p.setBusesLayout (l))
    {
        l.inputBuses.getReference (0) = l.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        p.setBusesLayout (l);
    }
    p.setRateAndBufferSizeDetails (sampleRate, maxBlock);
    p.prepareToPlay (sampleRate, maxBlock);
    s.preparedLatency = p.getLatencySamples();
}

void Rack::prepare (double sr, int block)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    maxBlock = juce::jmax (16, block);

    auto prepAll = [this] (Chain& c) { for (auto& s : c.slots) prepareSlot (*s, c); };
    for (auto& c : tracks)  prepAll (*c);
    for (auto& c : returns) prepAll (*c);
    prepAll (*mixChain);
    rebuild();
}

std::unique_ptr<Rack::Render> Rack::build()
{
    auto r = std::make_unique<Render>();

    auto fill = [this] (RChain& rc, Chain& c)
    {
        rc.chain = &c;
        int bufCh = c.numChannels();
        for (auto& s : c.slots)
        {
            if (s->processor == nullptr) continue;
            RSlot rs;
            rs.slot = s.get();
            rs.p = s->processor.get();
            rs.channels = juce::jmax (rs.p->getTotalNumInputChannels(), rs.p->getTotalNumOutputChannels(), 1);
            s->preparedLatency = rs.p->getLatencySamples();
            rs.latency = s->preparedLatency;
            rs.bypassDelay.setDelay (rs.latency, c.numChannels());
            bufCh = juce::jmax (bufCh, rs.channels);
            rc.latency += rs.latency;
            rc.slots.push_back (std::move (rs));
        }
        rc.buf.setSize (juce::jmax (2, bufCh), maxBlock);
        rc.midi.ensureSize (256);
        rc.trimCur = dbToGain (c.trimDb.load());
        rc.levelCur = dbToGain (c.levelDb.load());
    };

    r->tracks.resize ((size_t) maxTracks);
    int latest = 0;
    for (int i = 0; i < maxTracks; ++i)
    {
        fill (r->tracks[(size_t) i], *tracks[(size_t) i]);
        latest = juce::jmax (latest, r->tracks[(size_t) i].latency);
    }
    for (auto& t : r->tracks)
        t.align.setDelay (latest - t.latency, 1);
    r->trackLatency = latest;

    for (int i = 0; i < numReturns; ++i) fill (r->returns[i], *returns[(size_t) i]);
    fill (r->mix, *mixChain);
    return r;
}

void Rack::rebuild (std::vector<std::unique_ptr<Slot>> removed)
{
    auto r = build();
    for (auto& s : removed) r->graveyard.push_back (std::move (s));   // see below
    currentTrackLatency = r->trackLatency;

    // The removed slots may still be in use in the OLD render: move them there, so they go with it.
    auto* old = current.exchange (r.release());
    if (old != nullptr)
    {
        auto* fresh = current.load();
        for (auto& s : fresh->graveyard) old->graveyard.push_back (std::move (s));
        fresh->graveyard.clear();
        retired.emplace_back (old);
    }
    freeRetired();
}

void Rack::freeRetired()
{
    auto* busy = inUse.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [busy] (const std::unique_ptr<Render>& r) { return r.get() != busy; }),
                   retired.end());
}

void Rack::housekeeping()
{
    freeRetired();

    // a plugin changed its latency (a lookahead switched on, say): line everything up again
    bool changed = false;
    auto check = [&changed] (Chain& c)
    {
        for (auto& s : c.slots)
            if (s->processor != nullptr && s->processor->getLatencySamples() != s->preparedLatency)
                changed = true;
    };
    for (auto& c : tracks)  check (*c);
    for (auto& c : returns) check (*c);
    check (*mixChain);
    if (changed) rebuild();
}

//==============================================================================
void Rack::process (const IO& io) noexcept
{
    Render* r = nullptr;
    do { r = current.load(); inUse.store (r); } while (r != current.load());
    if (r == nullptr) { inUse.store (nullptr); return; }

    const int n = juce::jmin (io.numSamples, maxBlock);
    juce::ScopedNoDenormals noDenormals;

    // ---- tracks
    const int numTracks = juce::jlimit (0, maxTracks, io.numTracks);
    for (int t = 0; t < numTracks; ++t)
    {
        auto& rc = r->tracks[(size_t) t];
        auto* x = rc.buf.getWritePointer (0);
        if (io.trackIn != nullptr && io.trackIn[t] != nullptr) juce::FloatVectorOperations::copy (x, io.trackIn[t], n);
        else juce::FloatVectorOperations::clear (x, n);

        applyGain (rc.buf.getArrayOfWritePointers(), 1, n, rc.trimCur, dbToGain (rc.chain->trimDb.load (std::memory_order_relaxed)));
        meter (rc.chain->peakIn, peakOf (x, n));
        if (! rc.slots.empty()) runChain (rc, 1, n);
        rc.align.process (rc.buf.getArrayOfWritePointers(), 1, n);
        meter (rc.chain->peakOut, peakOf (x, n));

        if (io.trackOut != nullptr && io.trackOut[t] != nullptr) juce::FloatVectorOperations::copy (io.trackOut[t], x, n);
    }

    // ---- stereo chains
    auto stereo = [n] (RChain& rc, const float* const* in, float* const* out)
    {
        auto* const* ch = rc.buf.getArrayOfWritePointers();
        for (int c = 0; c < 2; ++c)
            if (in[c] != nullptr) juce::FloatVectorOperations::copy (ch[c], in[c], n);
            else juce::FloatVectorOperations::clear (ch[c], n);

        meter (rc.chain->peakIn, juce::jmax (peakOf (ch[0], n), peakOf (ch[1], n)));
        if (! rc.slots.empty()) runChain (rc, 2, n);
        applyGain (ch, 2, n, rc.levelCur, dbToGain (rc.chain->levelDb.load (std::memory_order_relaxed)));
        meter (rc.chain->peakOut, juce::jmax (peakOf (ch[0], n), peakOf (ch[1], n)));

        for (int c = 0; c < 2; ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::copy (out[c], ch[c], n);
    };

    for (int i = 0; i < numReturns; ++i)
        stereo (r->returns[i], io.sends[i], io.returnsOut[i]);
    stereo (r->mix, io.mixIn, io.mixOut);

    inUse.store (nullptr);
}

} // namespace emerson

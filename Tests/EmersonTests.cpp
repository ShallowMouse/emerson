// Headless tests for EMERSON's engine: the insert chains and their latency alignment,
// mono tracks through stereo plugins, saving and loading, Hawthorne Route's channel
// names, the studio plugin list and the EMERSON Link (both ends, in one process).

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/Engine/Rack.h"
#include "../Source/Engine/RackState.h"
#include "../Source/Engine/RouteMap.h"
#include "../Source/Engine/StudioPlugins.h"
#include "../Source/Link/LinkHost.h"
#include "../Source/Link/LinkClientCore.h"
#include <iostream>

using namespace emerson;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond << std::endl; } } while (0)
static bool near (float a, float b, float tol = 1.0e-5f) { return std::abs (a - b) <= tol; }

//==============================================================================
/** A test plugin: multiplies by 'gain' and delays by 'latency' samples (and says so). */
class TestPlugin : public juce::AudioProcessor
{
public:
    TestPlugin (float g, int lat, bool monoOk)
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          gain (g), lat (lat), mono (monoOk) {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto o = l.getMainOutputChannelSet();
        if (o == juce::AudioChannelSet::mono() && ! mono) return false;
        return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet() == o;
    }
    void prepareToPlay (double, int) override { setLatencySamples (lat); delay.setDelay (lat, 2); ++prepared; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const int ch = getTotalNumOutputChannels();
        b.applyGain (gain);
        delay.process (b.getArrayOfWritePointers(), ch, b.getNumSamples());
        channelsSeen = ch;
    }
    const juce::String getName() const override { return "Test"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& m) override { m.append (&gain, sizeof (gain)); }
    void setStateInformation (const void* d, int n) override { if (n == sizeof (gain)) std::memcpy (&gain, d, sizeof (gain)); }

    float gain;
    int lat;
    bool mono;
    int prepared = 0, channelsSeen = 0;
    SampleDelay delay;
};

static std::unique_ptr<Slot> makeSlot (Rack& rack, Chain& c, float gain, int lat, bool monoOk = true)
{
    auto s = std::make_unique<Slot>();
    s->uid = rack.nextUid();
    s->identifier = "test:" + juce::String (gain) + ":" + juce::String (lat) + ":" + (monoOk ? "m" : "s");
    s->name = "Test";
    s->processor = std::make_unique<TestPlugin> (gain, lat, monoOk);
    rack.prepareSlot (*s, c);
    return s;
}

/** Runs one block of impulses on tracks 1-3 and returns where each track's impulse came out. */
struct Run
{
    std::vector<std::vector<float>> out;
    void go (Rack& rack, int tracks, int n, bool impulse = true)
    {
        std::vector<std::vector<float>> in ((size_t) tracks, std::vector<float> ((size_t) n, 0.0f));
        out.assign ((size_t) tracks, std::vector<float> ((size_t) n, 0.0f));
        std::vector<const float*> ip;
        std::vector<float*> op;
        for (int t = 0; t < tracks; ++t)
        {
            if (impulse) in[(size_t) t][0] = 1.0f;
            ip.push_back (in[(size_t) t].data());
            op.push_back (out[(size_t) t].data());
        }
        Rack::IO io;
        io.numTracks = tracks;
        io.trackIn = ip.data();
        io.trackOut = op.data();
        io.numSamples = n;
        rack.process (io);
    }
    int peakAt (int t) const
    {
        auto& v = out[(size_t) t];
        return (int) (std::max_element (v.begin(), v.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - v.begin());
    }
};

//==============================================================================
static void testLatencyAlignment()
{
    Rack rack;
    rack.prepare (48000.0, 256);

    auto& t1 = rack.track (0);
    t1.slots.push_back (makeSlot (rack, t1, 1.0f, 64));
    auto& t2 = rack.track (1);
    t2.slots.push_back (makeSlot (rack, t2, 1.0f, 16));
    t2.slots.push_back (makeSlot (rack, t2, 0.5f, 8));
    rack.rebuild();

    CHECK (rack.trackLatency() == 64);
    Run r;
    r.go (rack, 3, 256);
    CHECK (r.peakAt (0) == 64);       // its own plugin
    CHECK (r.peakAt (1) == 64);       // 24 of plugins + 40 of alignment
    CHECK (r.peakAt (2) == 64);       // empty: aligned
    CHECK (near (r.out[1][64], 0.5f));

    // bypassing keeps the timing (the bypassed plugin's latency is still there)
    t1.slots[0]->bypassed = true;
    t2.bypassAll = true;
    r.go (rack, 3, 256);
    CHECK (r.peakAt (0) == 64);
    CHECK (r.peakAt (1) == 64);
    CHECK (near (r.out[1][64], 1.0f));   // the 0.5 plugin is bypassed
    CHECK (r.peakAt (2) == 64);

    // the latest chain goes: everything moves up
    std::vector<std::unique_ptr<Slot>> removed;
    removed.push_back (std::move (t1.slots[0]));
    t1.slots.clear();
    rack.rebuild (std::move (removed));
    CHECK (rack.trackLatency() == 24);
    r.go (rack, 3, 256);
    CHECK (r.peakAt (0) == 24);
    CHECK (r.peakAt (2) == 24);
}

static void testMonoTracks()
{
    Rack rack;
    rack.prepare (44100.0, 128);
    auto& t = rack.track (0);
    t.slots.push_back (makeSlot (rack, t, 2.0f, 0, true));    // runs mono
    t.slots.push_back (makeSlot (rack, t, 3.0f, 0, false));   // stereo only: both sides, averaged back
    rack.rebuild();
    CHECK (static_cast<TestPlugin*> (t.slots[0]->processor.get())->getTotalNumOutputChannels() == 1);
    CHECK (static_cast<TestPlugin*> (t.slots[1]->processor.get())->getTotalNumOutputChannels() == 2);

    Run r;
    r.go (rack, 1, 128);
    CHECK (near (r.out[0][0], 6.0f));

    // trim, smoothed over a block, then steady
    t.trimDb = -6.0206f;
    r.go (rack, 1, 128);
    r.go (rack, 1, 128);
    CHECK (near (r.out[0][0], 3.0f, 1.0e-3f));
}

static void testStereoChains()
{
    Rack rack;
    rack.prepare (48000.0, 64);
    auto& ret = rack.fxReturn (0);
    ret.slots.push_back (makeSlot (rack, ret, 0.25f, 0));
    rack.rebuild();
    CHECK (static_cast<TestPlugin*> (ret.slots[0]->processor.get())->getTotalNumOutputChannels() == 2);

    std::vector<float> sL (64, 1.0f), sR (64, -1.0f), oL (64), oR (64), mL (64, 0.5f), mOutL (64), mOutR (64);
    Rack::IO io;
    io.numSamples = 64;
    io.sends[0][0] = sL.data(); io.sends[0][1] = sR.data();
    io.returnsOut[0][0] = oL.data(); io.returnsOut[0][1] = oR.data();
    io.mixIn[0] = mL.data();
    io.mixOut[0] = mOutL.data(); io.mixOut[1] = mOutR.data();
    rack.process (io);
    CHECK (near (oL[10], 0.25f) && near (oR[10], -0.25f));
    CHECK (near (mOutL[10], 0.5f) && near (mOutR[10], 0.0f));   // empty mix chain: straight through, missing side silent

    rack.mix().levelDb = -60.0f;                                 // off
    rack.process (io);
    rack.process (io);
    CHECK (near (mOutL[10], 0.0f));
}

static void testRemovedPluginsOutliveTheAudio()
{
    Rack rack;
    rack.prepare (48000.0, 64);
    auto& t = rack.track (0);
    t.slots.push_back (makeSlot (rack, t, 1.0f, 0));
    rack.rebuild();

    std::vector<std::unique_ptr<Slot>> removed;
    removed.push_back (std::move (t.slots[0]));
    t.slots.clear();
    rack.rebuild (std::move (removed));   // freed straight away (nothing is processing)
    Run r;
    r.go (rack, 1, 64);
    CHECK (r.peakAt (0) == 0);
    rack.housekeeping();
}

static void testLatencyChangeRebuilds()
{
    Rack rack;
    rack.prepare (48000.0, 256);
    auto& t = rack.track (0);
    t.slots.push_back (makeSlot (rack, t, 1.0f, 10));
    rack.rebuild();
    CHECK (rack.trackLatency() == 10);
    static_cast<TestPlugin*> (t.slots[0]->processor.get())->setLatencySamples (30);
    rack.housekeeping();
    CHECK (rack.trackLatency() == 30);
}

//==============================================================================
static void testSaveAndLoad()
{
    Rack rack;
    rack.prepare (48000.0, 128);
    Session session;
    auto& t = rack.track (4);
    t.slots.push_back (makeSlot (rack, t, 0.7f, 0));
    t.slots[0]->bypassed = true;
    t.trimDb = 3.0f;
    session.names.set (4, "Lead vox");
    session.directOut[4] = 6;
    session.output = Session::Output::directOuts;
    rack.fxReturn (1).levelDb = -4.0f;
    rack.mix().slots.push_back (makeSlot (rack, rack.mix(), 0.9f, 0));
    static_cast<TestPlugin*> (rack.mix().slots[0]->processor.get())->gain = 0.95f;   // state differs from the identifier
    rack.rebuild();

    auto xml = RackState::toXml (rack, session);
    CHECK (xml->getNumChildElements() == 1 + 2 + 1);   // only track 5 is kept, plus the returns and the mix

    RackState::Loader loader = [] (const juce::String& id, juce::String& err) -> std::unique_ptr<juce::AudioProcessor>
    {
        auto p = juce::StringArray::fromTokens (id, ":", {});
        if (p.size() != 4 || p[0] != "test") { err = "unknown"; return nullptr; }
        return std::make_unique<TestPlugin> (p[1].getFloatValue(), p[2].getIntValue(), p[3] == "m");
    };

    Rack back;
    back.prepare (48000.0, 128);
    Session s2;
    juce::StringArray missing;
    auto* bad = xml->createNewChildElement ("CHAIN");
    bad->setAttribute ("kind", "track");
    bad->setAttribute ("index", 9);
    bad->createNewChildElement ("SLOT")->setAttribute ("id", "gone:1");
    RackState::fromXml (*xml, back, s2, loader, missing);

    CHECK (back.track (4).slots.size() == 1);
    CHECK (back.track (4).slots[0]->bypassed.load());
    CHECK (near (back.track (4).trimDb.load(), 3.0f));
    CHECK (s2.names[4] == "Lead vox");
    CHECK (s2.directOut[4] == 6);
    CHECK (s2.output == Session::Output::directOuts);
    CHECK (near (back.fxReturn (1).levelDb.load(), -4.0f));
    CHECK (back.mix().slots.size() == 1);
    CHECK (near (static_cast<TestPlugin*> (back.mix().slots[0]->processor.get())->gain, 0.95f));
    CHECK (missing.size() == 1);
    CHECK (back.track (9).slots.empty());

    RackState::clear (back);
    CHECK (back.track (4).slots.empty() && back.mix().slots.empty());
}

//==============================================================================
static void testRouteNames()
{
    juce::StringArray ins { "Hawthorne track 1", "Hawthorne track 2", "Hawthorne track 3", "Hawthorne master L", "Hawthorne master R", "Hawthorne LTC" };
    juce::StringArray outs { "To Hawthorne track 1", "To Hawthorne track 2", "To Hawthorne track 3", "To Hawthorne master L", "To Hawthorne master R" };
    juce::BigInteger allIn, allOut, someIn;
    allIn.setRange (0, ins.size(), true);
    allOut.setRange (0, outs.size(), true);
    auto m = RouteMap::make (ins, allIn, outs, allOut);
    CHECK (m.isHawthorneRoute && m.numTracks == 3);
    CHECK (m.trackIn[0] == 0 && m.trackIn[2] == 2 && m.trackIn[3] == -1);
    CHECK (m.trackOut[1] == 1 && m.masterOut[0] == 3 && m.masterOut[1] == 4);

    someIn.setBit (1); someIn.setBit (2);   // only tracks 2 and 3 switched on: indices among the active ones
    m = RouteMap::make (ins, someIn, outs, allOut);
    CHECK (m.trackIn[0] == -1 && m.trackIn[1] == 0 && m.trackIn[2] == 1);

    m = RouteMap::make ({ "Line 1", "Line 2" }, allIn, { "Out 1", "Out 2" }, allOut);
    CHECK (! m.isHawthorneRoute && m.masterOut[0] == -1);
}

static void testStudioList()
{
    CHECK (StudioPlugins::offers ("RS124"));
    CHECK (StudioPlugins::offers ("Plate 140"));
    CHECK (StudioPlugins::offers ("echo chamber"));      // case and spacing don't matter
    CHECK (StudioPlugins::offers ("Echorec", "PEQUOD Pinkfloyd"));
    CHECK (! StudioPlugins::offers ("AC30 Top Boost"));   // amps
    CHECK (! StudioPlugins::offers ("Big Muff"));         // pedals
    CHECK (! StudioPlugins::offers ("Wah"));
    CHECK (! StudioPlugins::offers ("Studio Mics"));
    CHECK (! StudioPlugins::offers ("Tape Echo", "PEQUOD Pedals"));

    // nothing listed twice in different groups
    juce::StringArray seen;
    bool dup = false;
    for (auto& g : StudioPlugins::groups())
        for (auto& n : juce::StringArray::fromTokens (g.plugins, "|", {}))
        {
            if (seen.contains (StudioPlugins::normalise (n))) dup = true;
            seen.add (StudioPlugins::normalise (n));
        }
    CHECK (! dup);
}

//==============================================================================
static void testLink()
{
    auto mem = std::make_unique<link::Shared>();
    std::memset ((void*) mem.get(), 0, sizeof (link::Shared));
    LinkHost host;
    host.attach (mem.get());
    host.start (48000, 64, 3);
    link::ClientCore client (mem.get());

    constexpr int n = 64;
    std::vector<std::vector<float>> toW (link::toWhitmanChannels, std::vector<float> (n)), fromW (link::fromWhitmanChannels, std::vector<float> (n));
    std::vector<std::vector<float>> cIn (link::toWhitmanChannels, std::vector<float> (link::maxBlock)), cOut (link::fromWhitmanChannels, std::vector<float> (link::maxBlock));
    std::vector<const float*> toP;   std::vector<float*> fromP, cInP, cOutP;
    for (auto& v : toW) toP.push_back (v.data());
    for (auto& v : fromW) fromP.push_back (v.data());
    for (auto& v : cIn) cInP.push_back (v.data());
    for (auto& v : cOut) cOutP.push_back (v.data());

    // WHITMAN: mix = track 1 + track 2 on the left, return A L on the right; send A = track 3
    auto mixer = [] (const float* const* in, float* const* out, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            out[link::fromMixLeft][i] = in[0][i] + in[1][i];
            out[link::fromMixRight][i] = in[link::toReturnA][i];
            out[link::fromSendA][i] = in[2][i];
            out[link::fromSendA + 1][i] = 0;
            out[link::fromSendB][i] = out[link::fromSendB + 1][i] = 0;
        }
    };

    mem->clientRunning = 1;
    client.pump (cInP.data(), cOutP.data(), link::maxBlock, mixer);   // starts following

    int dropBefore = host.dropouts();
    for (int block = 0; block < 300; ++block)   // goes round the ring
    {
        host.readFromWhitman (fromP.data(), n);
        if (block > 0)
        {
            // what came back is last block's: tracks 1 + 2 = 2 * (block - 1) + 1
            const float expect = (float) (2 * (block - 1) + 1);
            CHECK (near (fromW[link::fromMixLeft][n - 1], expect));
            CHECK (near (fromW[link::fromSendA][0], (float) (block - 1) * 10.0f));
            CHECK (near (fromW[link::fromMixRight][5], (float) (block - 1) * -1.0f));
        }
        for (int i = 0; i < n; ++i)
        {
            toW[0][(size_t) i] = (float) block;
            toW[1][(size_t) i] = (float) block + 1.0f;
            toW[2][(size_t) i] = (float) block * 10.0f;
            toW[link::toReturnA][(size_t) i] = (float) -block;
        }
        host.writeToWhitman (toP.data(), 3, n);
        CHECK (client.pump (cInP.data(), cOutP.data(), link::maxBlock, mixer) == n);
    }
    CHECK (host.dropouts() == dropBefore);

    // WHITMAN misses a block: silence and a dropout, then it catches up
    host.writeToWhitman (toP.data(), 3, n);
    host.readFromWhitman (fromP.data(), n);
    CHECK (host.dropouts() == dropBefore + 1);
    CHECK (near (fromW[link::fromMixLeft][0], 0.0f));
    CHECK (client.pump (cInP.data(), cOutP.data(), 32, mixer) == n);   // in two halves of 32

    // tracks above the reel's count go out silent
    toW[5][0] = 9.0f;
    host.writeToWhitman (toP.data(), 3, n);
    client.pump (cInP.data(), cOutP.data(), link::maxBlock, [&] (const float* const* in, float* const* out, int f) { CHECK (in[5][0] == 0.0f); mixer (in, out, f); });

    // WHITMAN not there: silence, no dropouts counted
    mem->clientRunning = 0;
    const int d = host.dropouts();
    host.writeToWhitman (toP.data(), 3, n);
    host.readFromWhitman (fromP.data(), n);
    CHECK (host.dropouts() == d && near (fromW[0][0], 0.0f));
}

static void testDirectOutPatch()
{
    directout::Patch p {};
    DirectOutPatch d;
    d.attach (&p);
    int outs[4] = { 2, -1, 5, 7 };
    d.publish (true, 4, outs);
    CHECK (p.seq.load() % 2 == 0 && p.seq.load() > 0);
    CHECK (p.enabled == 1 && p.numTracks == 4);
    CHECK (p.output[0] == 2 && p.output[1] == -1 && p.output[3] == 7 && p.output[4] == -1);
    const auto s = p.seq.load();
    d.publish (false, 4, outs);
    CHECK (p.seq.load() == s + 2 && p.enabled == 0);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testLatencyAlignment();
    testMonoTracks();
    testStereoChains();
    testRemovedPluginsOutliveTheAudio();
    testLatencyChangeRebuilds();
    testSaveAndLoad();
    testRouteNames();
    testStudioList();
    testLink();
    testDirectOutPatch();
    std::cout << checks << " checks, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}

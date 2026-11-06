#pragma once

#include <juce_core/juce_core.h>
#include <cstring>
#include <vector>

namespace emerson
{

//==============================================================================
/** Which of the audio device's (active) channels are what, from Hawthorne Route's
    channel names: "Hawthorne track 7" in, "To Hawthorne track 7" / "To Hawthorne master L" out. */
struct RouteMap
{
    static constexpr int maxTracks = 96;

    std::vector<int> trackIn;        // per track: ACTIVE input index, or -1
    std::vector<int> trackOut;       // per track: ACTIVE output index ("To Hawthorne track N"), or -1
    int masterOut[2] { -1, -1 };     // ACTIVE output indices of "To Hawthorne master L / R"
    int numTracks = 0;               // highest track the device shows
    bool isHawthorneRoute = false;

    /** "Hawthorne track 12" -> 12 (with prefix "Hawthorne track "), else 0. */
    static int numberAfter (const juce::String& name, const char* prefix)
    {
        const auto t = name.trim();
        if (! t.startsWithIgnoreCase (prefix)) return 0;
        const int n = t.substring ((int) std::strlen (prefix)).trim().getIntValue();
        return n >= 1 && n <= maxTracks ? n : 0;
    }

    static RouteMap make (const juce::StringArray& inputNames, const juce::BigInteger& activeIn,
                          const juce::StringArray& outputNames, const juce::BigInteger& activeOut)
    {
        RouteMap m;
        m.trackIn.assign ((size_t) maxTracks, -1);
        m.trackOut.assign ((size_t) maxTracks, -1);

        int a = 0;
        for (int ch = 0; ch < inputNames.size(); ++ch)
        {
            if (! activeIn[ch]) continue;
            if (const int t = numberAfter (inputNames[ch], "Hawthorne track "); t > 0)
            {
                m.trackIn[(size_t) t - 1] = a;
                m.numTracks = juce::jmax (m.numTracks, t);
                m.isHawthorneRoute = true;
            }
            ++a;
        }

        a = 0;
        for (int ch = 0; ch < outputNames.size(); ++ch)
        {
            if (! activeOut[ch]) continue;
            const auto n = outputNames[ch].trim();
            if (const int t = numberAfter (n, "To Hawthorne track "); t > 0) m.trackOut[(size_t) t - 1] = a;
            if (n.equalsIgnoreCase ("To Hawthorne master L")) m.masterOut[0] = a;
            if (n.equalsIgnoreCase ("To Hawthorne master R")) m.masterOut[1] = a;
            ++a;
        }
        return m;
    }
};

} // namespace emerson

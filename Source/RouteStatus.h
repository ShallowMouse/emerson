#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace emerson
{

/** A read-only look at Hawthorne Route's shared memory (Source/Route/RouteShared.h,
    a copy of Hawthorne's), just to tell the user whether Hawthorne is running and how
    many tracks its reel has. The audio itself goes through the ASIO driver. Windows only. */
class RouteStatus
{
public:
    RouteStatus() = default;
    ~RouteStatus();
    RouteStatus (const RouteStatus&) = delete;

    struct Info
    {
        bool hawthorneRunning = false;   // Hawthorne has Route open and its audio is running
        int  tracks = 0;                 // the reel's track count (0 if not running)
        int  sampleRate = 0;

        // from Hawthorne's info block (a Hawthorne new enough to send it, with a reel loaded)
        bool hasInfo = false;
        juce::String reelName;
        juce::StringArray trackNames;    // per track; empty = not named in Hawthorne
        std::vector<bool> armed, toMix;  // per track

        bool sameAs (const Info& o) const
        {
            return hawthorneRunning == o.hawthorneRunning && tracks == o.tracks && sampleRate == o.sampleRate
                && hasInfo == o.hasInfo && reelName == o.reelName && trackNames == o.trackNames && armed == o.armed && toMix == o.toMix;
        }
    };

    /** Message thread; cheap enough to call from a timer. */
    Info poll();

private:
    void* mapping = nullptr;
    const void* view = nullptr;
    void* infoMapping = nullptr;
    const void* infoView = nullptr;
    void close();
    void readInfo (Info&);
};

} // namespace emerson

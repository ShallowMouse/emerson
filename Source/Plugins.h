#pragma once

#include <JuceHeader.h>
#include "Engine/StudioPlugins.h"

namespace emerson
{

//==============================================================================
/** The PEQUOD plugins EMERSON offers. Most run natively, from the PEQUOD engine (the
    same engine and panels as PEQUOD Host: nothing to scan). Collections that aren't in
    the engine (Pink Floyd's, Joe Meek's, Albini's...) are loaded from their installed
    VST3 files: EMERSON looks for the studio ones among them (scanVst3). */
namespace Plugins
{
    struct Entry
    {
        juce::String identifier;     // what's saved: "pequod:<id>" (built in) or "vst3:<JUCE identifier>"
        juce::String name;
        juce::String collection;     // "Beatles · Studio Two"
        int group = -1;              // StudioPlugins::groups()
        juce::Colour colour;
        bool native = true;
    };

    /** Every studio plugin there is, grouped and sorted. */
    const std::vector<Entry>& studio();
    const Entry* find (const juce::String& identifier);
    bool isNative (const juce::String& identifier);

    /** Makes one (message thread). */
    std::unique_ptr<juce::AudioProcessor> create (const juce::String& identifier, juce::String& error);

    /** The colour of a slot's plugin (its group's), or the accent. */
    juce::Colour colourOf (const juce::String& identifier);

    /** Looks through the VST3 folders for PEQUOD studio plugins the engine doesn't have
        (only files named like one are opened). Returns how many it found. Message thread. */
    int scanVst3 (const juce::FileSearchPath& extraFolders = {});
    /** What scanVst3 found, to keep between runs. */
    std::unique_ptr<juce::XmlElement> vst3ListToXml();
    void vst3ListFromXml (const juce::XmlElement&);
}

} // namespace emerson

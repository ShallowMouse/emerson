#pragma once

#include "Rack.h"
#include <functional>

namespace emerson
{

//==============================================================================
/** The settings that aren't plugins: where the tracks go. */
struct Session
{
    enum class Output { whitman = 0, directOuts = 1 };
    Output output = Output::whitman;
    std::vector<int> directOut = std::vector<int> ((size_t) Rack::maxTracks, -1);   // per track: interface output (0-based), -1 none
    juce::StringArray names = juce::StringArray();                                   // per track, typed in EMERSON (empty = Hawthorne's)

    Session() { names.ensureStorageAllocated (Rack::maxTracks); for (int i = 0; i < Rack::maxTracks; ++i) names.add ({}); }
};

/** Saving and loading a whole rack (.emerson files, and the one kept between runs). */
namespace RackState
{
    /** Makes a plugin from what was saved; nullptr (and an error) if it can't. */
    using Loader = std::function<std::unique_ptr<juce::AudioProcessor> (const juce::String& identifier, juce::String& error)>;

    std::unique_ptr<juce::XmlElement> toXml (Rack&, const Session&);

    /** Replaces the rack's chains with the saved ones (message thread; rebuilds the rack).
        Plugins that can't be loaded are left out; their names come back in 'missing'. */
    void fromXml (const juce::XmlElement&, Rack&, Session&, const Loader&, juce::StringArray& missing);

    /** Every chain empty, levels at 0 dB, nothing bypassed. Keeps the session's output. */
    void clear (Rack&);

    std::unique_ptr<juce::XmlElement> chainToXml (Chain&);
    /** Loads a chain's slots into 'into' (replacing its slots); removed slots come back in 'removed'. */
    void chainFromXml (const juce::XmlElement&, Rack&, Chain& into, const Loader&, juce::StringArray& missing,
                       std::vector<std::unique_ptr<Slot>>& removed, bool levels = true);

    juce::String kindName (ChainKind);
}

} // namespace emerson

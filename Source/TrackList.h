#pragma once

#include <JuceHeader.h>
#include "Engine/Rack.h"
#include "Engine/RackState.h"

namespace emerson
{

/** Which chain is on show. */
struct ChainRef
{
    ChainKind kind = ChainKind::track;
    int index = 0;
    bool operator== (const ChainRef& o) const { return kind == o.kind && index == o.index; }
    bool operator!= (const ChainRef& o) const { return ! operator== (o); }
};

//==============================================================================
/** The left-hand column: one row per tape track (what's on its inserts, its meter,
    and in DIRECT OUTS its interface output), then the two FX returns and the mix. */
class TrackList : public juce::Component
{
public:
    TrackList (Rack&, Session&);
    ~TrackList() override;

    void setNumTracks (int n);
    int getNumTracks() const { return numTracks; }
    void setHawthorneNames (const juce::StringArray& names, const std::vector<bool>& armed);
    juce::String nameOf (int track) const;          // EMERSON's name, else Hawthorne's, else "Track n"

    void setSelected (ChainRef);
    ChainRef getSelected() const { return selected; }

    void refresh();                                 // inserts / names changed
    void updateMeters();                            // timer

    std::function<void (ChainRef)> onSelect;
    std::function<void (ChainRef, juce::Point<int>)> onMenu;     // right click
    std::function<void (int track)> onPickDirectOut;             // the OUT box
    std::function<void (int track)> onRename;                    // double click on the name

    void resized() override;

    static constexpr int rowH = 34;

private:
    class Row;
    class Rows;
    Rack& rack;
    Session& session;
    int numTracks = 0;
    ChainRef selected;
    juce::StringArray hawthorneNames;
    std::vector<bool> armed;

    juce::Viewport viewport;
    std::unique_ptr<Rows> rows;
    juce::OwnedArray<Row> trackRows, fixedRows;

    Row* rowFor (ChainRef) const;
};

} // namespace emerson

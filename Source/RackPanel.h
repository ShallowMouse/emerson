#pragma once

#include <JuceHeader.h>
#include "Engine/Rack.h"
#include "TrackList.h"
#include "EmersonLookAndFeel.h"

namespace emerson
{

//==============================================================================
/** The right-hand side: the insert chain of whatever's picked in the track list,
    as a 19-inch rack. Top to bottom is the signal's order; every effect's own panel
    (drawn by the PEQUOD engine as in PEQUOD Host) is mounted in it, whole rack units high. */
class RackPanel : public juce::Component
{
public:
    explicit RackPanel (Rack&);
    ~RackPanel() override;

    /** Shows this chain. title: the track's name. */
    void show (ChainRef, const juce::String& title);
    ChainRef getShown() const { return shown; }
    void setTitle (const juce::String&);

    /** The chain was replaced from outside (paste, load): rebuild the view. */
    void chainReplaced();
    /** Closes the plugin panels (before plugins are deleted from outside). */
    void closeEditor();

    void copyChain();
    void pasteChain();
    void clearChain();
    bool hasClipboard() const { return clipboard != nullptr; }

    void updateMeters();

    std::function<void()> onChanged;                 // anything in the chain changed (save, list)
    std::function<std::unique_ptr<juce::AudioProcessor> (const juce::String&, juce::String&)> createPlugin;
    std::function<void()> onScanVst3;                 // "Look for PEQUOD VST3s" in the add menu

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Unit;
    class Units : public juce::Component {};
    class AddButton;

    Rack& rack;
    ChainRef shown;
    juce::String title;

    juce::Viewport unitView;
    Units units;
    juce::OwnedArray<Unit> unitRows;
    std::unique_ptr<AddButton> addButton;

    juce::Slider level;
    juce::Label levelLabel;
    juce::TextButton bypassAll { "BYPASS ALL" }, copyButton { "COPY" }, pasteButton { "PASTE" }, clearButton { "CLEAR" };

    juce::Rectangle<int> header;

    std::unique_ptr<juce::XmlElement> clipboard;

    Chain& chain() const { return rack.chain (shown.kind, shown.index); }
    void rebuildUnits();
    void showAddMenu (int insertAt);
    void addPlugin (const juce::String& identifier, int insertAt);
    void removeSlot (int uid);
    void moveSlot (int uid, int delta);
    void changed();
};

} // namespace emerson

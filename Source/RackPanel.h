#pragma once

#include <JuceHeader.h>
#include "Engine/Rack.h"
#include "TrackList.h"
#include "EmersonLookAndFeel.h"

namespace emerson
{

//==============================================================================
/** The right-hand side: the insert chain of whatever's picked in the track list.
    A column of units (top to bottom is the signal's order) and the picked unit's
    own panel beside it, drawn by the PEQUOD engine as in PEQUOD Host. */
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
    /** Closes the plugin panel (before plugins are deleted from outside). */
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
    int selectedUid = 0;

    juce::Viewport unitView;
    Units units;
    juce::OwnedArray<Unit> unitRows;
    std::unique_ptr<AddButton> addButton;

    juce::Slider level;
    juce::Label levelLabel;
    juce::TextButton bypassAll { "BYPASS ALL" }, copyButton { "COPY" }, pasteButton { "PASTE" }, clearButton { "CLEAR" };

    juce::Component editorHolder;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int editorUid = 0;
    bool editorScales = true;
    juce::Rectangle<int> editorArea, header;

    std::unique_ptr<juce::XmlElement> clipboard;

    Chain& chain() const { return rack.chain (shown.kind, shown.index); }
    void rebuildUnits();
    void select (int uid);
    void showEditorFor (int uid);
    void layoutEditor();
    void showAddMenu (int insertAt);
    void addPlugin (const juce::String& identifier, int insertAt);
    void removeSlot (int uid);
    void moveSlot (int uid, int delta);
    void changed();
};

} // namespace emerson

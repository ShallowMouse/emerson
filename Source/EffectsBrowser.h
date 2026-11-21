#pragma once

// EMERSON's effects browser, as IRVING's: down the right of the rack, every studio effect
// with a search box, grouped by what it does or by the collection it comes from. Drag one into
// the rack (between units, where it should go) or double-click it to add it at the bottom.

#include <JuceHeader.h>
#include "Plugins.h"

namespace emerson
{

class EffectsBrowser : public juce::Component,
                       private juce::ListBoxModel
{
public:
    EffectsBrowser();

    /** Rebuild the list (after a VST3 scan, say). */
    void refresh();

    std::function<void (const juce::String& identifier)> onAdd;     // double-clicked (or Enter)
    std::function<void()> onScanVst3;

    /** What a drag from here carries. */
    static constexpr const char* dragPrefix = "emerson-effect:";

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        bool header = false;
        juce::String text, sub;
        const Plugins::Entry* entry = nullptr;
        juce::Colour colour;
    };

    enum class Grouping { function, collection };
    Grouping grouping = Grouping::function;
    std::vector<Row> rows;

    juce::TextEditor search;
    juce::TextButton byFunction { "BY FUNCTION" }, byCollection { "BY COLLECTION" }, scanButton { "FIND VST3s" };
    juce::ListBox list { "effects", this };

    void setGrouping (Grouping);
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    juce::String getTooltipForRow (int row) override;
};

} // namespace emerson

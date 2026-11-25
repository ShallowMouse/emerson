#include "EffectsBrowser.h"
#include "EmersonLookAndFeel.h"

namespace emerson
{

EffectsBrowser::EffectsBrowser()
{
    for (auto* b : { &byFunction, &byCollection })
    {
        b->setColour (juce::TextButton::buttonColourId, Pequod::Colours::panel);
        addAndMakeVisible (b);
    }
    byFunction.setTooltip ("Group the effects by what they do: EQs, compressors, delays, reverbs...");
    byCollection.setTooltip ("Group the effects by the collection they come from");
    byFunction.onClick = [this] { setGrouping (Grouping::function); };
    byCollection.onClick = [this] { setGrouping (Grouping::collection); };

    scanButton.setTooltip ("Look for PEQUOD VST3s the built-in engine doesn't have (Pink Floyd, Joe Meek, Albini...)");
    scanButton.onClick = [this] { if (onScanVst3) onScanVst3(); };
    scanButton.setColour (juce::TextButton::buttonColourId, Pequod::Colours::panel);
   #if JUCE_PLUGINHOST_VST3
    addAndMakeVisible (scanButton);
   #endif

    search.setTextToShowWhenEmpty ("Search...", Pequod::Colours::textDim);
    search.setFont (Pequod::body (14.0f));
    search.onTextChange = [this] { refresh(); };
    search.onEscapeKey = [this] { search.clear(); refresh(); };
    search.onReturnKey = [this]
    {
        for (auto& r : rows)
            if (! r.header) { if (onAdd) onAdd (r.entry->identifier); return; }    // (the first match)
    };
    addAndMakeVisible (search);

    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, Pequod::Colours::well);
    addAndMakeVisible (list);

    setGrouping (Grouping::function);
}

void EffectsBrowser::setGrouping (Grouping g)
{
    grouping = g;
    byFunction.setToggleState (g == Grouping::function, juce::dontSendNotification);
    byCollection.setToggleState (g == Grouping::collection, juce::dontSendNotification);
    for (auto* b : { &byFunction, &byCollection })
        b->setColour (juce::TextButton::buttonColourId, b->getToggleState() ? Pequod::Colours::mustardLo : Pequod::Colours::panel);
    refresh();
}

void EffectsBrowser::refresh()
{
    rows.clear();
    const auto words = juce::StringArray::fromTokens (search.getText().trim().toLowerCase(), " ", "");
    const auto& groups = StudioPlugins::groups();
    auto groupName = [&] (int g) { return juce::isPositiveAndBelow (g, (int) groups.size()) ? juce::String (groups[(size_t) g].name) : juce::String ("Other"); };

    std::vector<const Plugins::Entry*> shown;
    for (auto& e : Plugins::studio())
    {
        const auto hay = (e.name + " " + e.collection + " " + groupName (e.group)).toLowerCase();
        bool ok = true;
        for (auto& w : words) if (! hay.contains (w)) { ok = false; break; }
        if (ok) shown.push_back (&e);
    }

    if (grouping == Grouping::collection)
        std::stable_sort (shown.begin(), shown.end(), [] (auto* a, auto* b)
        {
            if (a->collection != b->collection) return a->collection.compareNatural (b->collection) < 0;
            return a->name.compareNatural (b->name) < 0;
        });

    juce::String last = "\x01";
    for (auto* e : shown)
    {
        const auto head = grouping == Grouping::function ? groupName (e->group) : e->collection;
        if (head != last)
        {
            Row h;
            h.header = true;
            h.text = head.toUpperCase();
            h.colour = grouping == Grouping::function ? e->colour : juce::Colour();
            rows.push_back (h);
            last = head;
        }
        Row r;
        r.text = e->name;
        r.sub = grouping == Grouping::function ? e->collection : groupName (e->group);
        r.entry = e;
        r.colour = e->colour;
        rows.push_back (r);
    }
    if (shown.empty())
    {
        Row r;
        r.header = true;
        r.text = Plugins::studio().empty() ? "   No PEQUOD studio effects in this build" : "   Nothing matches";
        rows.push_back (r);
    }
    list.updateContent();
    list.repaint();
}

void EffectsBrowser::paintListBoxItem (int index, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (index, (int) rows.size())) return;
    auto& row = rows[(size_t) index];
    auto r = juce::Rectangle<int> (0, 0, w, h);

    if (row.header)
    {
        if (row.text.startsWith ("   "))
        {
            g.setColour (Pequod::Colours::textDim);
            g.setFont (Pequod::body (12.0f));
            g.drawText (row.text.trim(), r.reduced (16, 0), juce::Justification::centredLeft);
            return;
        }
        const bool tinted = ! row.colour.isTransparent();
        g.setColour (tinted ? row.colour.withAlpha (0.6f) : Pequod::Colours::mustardLo.withAlpha (0.5f));
        g.fillRect (r.removeFromBottom (tinted ? 2 : 1));
        g.setColour (tinted ? row.colour : Pequod::Colours::mustard);
        g.setFont (Pequod::heading (12.0f));
        g.drawText (row.text, r.reduced (10, 0), juce::Justification::bottomLeft, true);
        return;
    }

    if (selected)
    {
        g.setColour (Pequod::Colours::panelHi);
        g.fillRect (r);
        g.setColour (Pequod::Colours::mustard);
        g.fillRect (r.withWidth (3));
    }
    // a small LED in its group's colour
    g.setColour (row.colour.isTransparent() ? Pequod::Colours::mustard : row.colour);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ 10.0f, (float) h * 0.5f }));

    auto text = r.reduced (16, 0).withTrimmedLeft (4);
    auto sub = text.removeFromRight (juce::jmin (120, w * 2 / 5));
    g.setColour (Pequod::Colours::textDim);
    g.setFont (Pequod::heading (10.0f, false));
    g.drawText (row.sub.upToFirstOccurrenceOf ("\xc2\xb7", false, false).trim().toUpperCase(), sub, juce::Justification::centredRight, true);
    g.setColour (Pequod::Colours::text);
    g.setFont (Pequod::body (14.0f));
    g.drawText (row.text, text, juce::Justification::centredLeft, true);
}

void EffectsBrowser::listBoxItemDoubleClicked (int index, const juce::MouseEvent&) { returnKeyPressed (index); }

void EffectsBrowser::returnKeyPressed (int index)
{
    if (juce::isPositiveAndBelow (index, (int) rows.size()) && ! rows[(size_t) index].header && onAdd)
        onAdd (rows[(size_t) index].entry->identifier);
}

juce::var EffectsBrowser::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.size() == 0) return {};
    const int i = selected[0];
    if (! juce::isPositiveAndBelow (i, (int) rows.size()) || rows[(size_t) i].header) return {};
    return juce::String (dragPrefix) + rows[(size_t) i].entry->identifier;
}

juce::String EffectsBrowser::getTooltipForRow (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) rows.size()) || rows[(size_t) index].header) return {};
    auto& r = rows[(size_t) index];
    return r.text + "  (" + r.entry->collection + ")\nDrag it into the rack, or double-click to add it at the bottom";
}

void EffectsBrowser::paint (juce::Graphics& g)
{
    g.setColour (Pequod::Colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (Pequod::Colours::mustard);
    g.setFont (Pequod::heading (12.0f));
    g.drawText ("EFFECTS", getLocalBounds().reduced (12, 0).removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void EffectsBrowser::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (24);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto groupRow = r.removeFromTop (24);
    byFunction.setBounds (groupRow.removeFromLeft (groupRow.getWidth() / 2).reduced (1, 0));
    byCollection.setBounds (groupRow.reduced (1, 0));
    r.removeFromTop (6);
    if (scanButton.isVisible())
    {
        scanButton.setBounds (r.removeFromBottom (26));
        r.removeFromBottom (6);
    }
    list.setBounds (r);
}

} // namespace emerson

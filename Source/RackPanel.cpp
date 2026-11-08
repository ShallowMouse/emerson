#include "RackPanel.h"
#include "Engine/RackState.h"
#include "Plugins.h"

namespace emerson
{

// Each effect is drawn as a 1U, 19-inch rack unit: 482.6 mm wide (ears included) by 44.45 mm high,
// scaled to the column. unitGap is the space between units, outside the panels.
static constexpr double rackUnitRatio = 44.45 / 482.6;
static constexpr int unitGap = 2;
static int unitsWidthFor (int panelWidth)  { return juce::jlimit (380, 560, juce::roundToInt (panelWidth * 0.42)); }
static int unitHeightFor (int unitWidth)   { return juce::roundToInt (unitWidth * rackUnitRatio); }

//==============================================================================
class RackPanel::Unit : public juce::Component
{
public:
    Unit (RackPanel& o, Slot& s) : owner (o), slot (s), colour (Plugins::colourOf (s.identifier))
    {
        power.setToggleState (! s.bypassed.load(), juce::dontSendNotification);
        power.setTooltip ("On / bypassed (bypassed keeps its delay, so the tracks stay in time)");
        power.onClick = [this] { slot.bypassed = ! power.getToggleState(); repaint(); owner.changed(); };
        addAndMakeVisible (power);

        for (auto* b : { &up, &down, &remove })
        {
            b->setWantsKeyboardFocus (false);
            b->setColour (juce::TextButton::buttonColourId, Pequod::Colours::well);
            addAndMakeVisible (b);
        }
        up.setTooltip ("Move up (earlier in the chain)");
        down.setTooltip ("Move down (later in the chain)");
        remove.setTooltip ("Take it out of the chain");
        up.onClick     = [this] { owner.moveSlot (slot.uid, -1); };
        down.onClick   = [this] { owner.moveSlot (slot.uid, +1); };
        remove.onClick = [this] { owner.removeSlot (slot.uid); };
    }

    int uid() const { return slot.uid; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool sel = owner.selectedUid == slot.uid;
        const bool on = ! slot.bypassed.load() && ! owner.chain().bypassAll.load();

        g.setColour (Pequod::Colours::rail);
        g.fillRoundedRectangle (r, 3.0f);
        const float ear = r.getWidth() * (15.88f / 482.6f);
        auto face = r.reduced (ear, 0);
        g.setGradientFill (juce::ColourGradient (colour.withMultipliedBrightness (sel ? 0.85f : 0.6f), face.getX(), face.getY(),
                                                 colour.withMultipliedBrightness (sel ? 0.55f : 0.35f), face.getX(), face.getBottom(), false));
        g.fillRect (face);
        if (sel)
        {
            g.setColour (Pequod::Colours::mustard);
            g.drawRect (face, 2.0f);
        }
        // the rack holes, 31.75 mm apart, centred on the unit
        const float hole = r.getHeight() * (31.75f / 44.45f) * 0.5f, screw = juce::jmax (2.0f, r.getHeight() * 0.06f);
        for (float x : { r.getX() + ear * 0.5f, r.getRight() - ear * 0.5f })
        {
            Pequod::drawScrew (g, { x, r.getCentreY() - hole }, screw);
            Pequod::drawScrew (g, { x, r.getCentreY() + hole }, screw);
        }

        auto text = face.toNearestInt().reduced (0, 2);
        text.removeFromLeft (juce::roundToInt (r.getHeight() * 0.8f));
        text.removeFromRight (juce::roundToInt (r.getHeight() * 1.6f));
        const float h = (float) r.getHeight();
        g.setFont (Pequod::heading (juce::jlimit (11.0f, 15.0f, h * 0.34f)));
        g.setColour (on ? Pequod::Colours::text : Pequod::Colours::text.withAlpha (0.45f));
        g.drawText (slot.name.toUpperCase(), text.removeFromTop (text.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);
        g.setFont (Pequod::body (juce::jlimit (9.0f, 11.0f, h * 0.25f)));
        g.setColour (Pequod::Colours::text.withAlpha (on ? 0.7f : 0.35f));
        auto sub = slot.collection;
        if (const int lat = slot.preparedLatency; lat > 0)
            sub << "   " << juce::String (1000.0 * lat / juce::jmax (1.0, owner.rack.getSampleRate()), 1) << " ms";
        g.drawText (sub, text, juce::Justification::topLeft, true);
    }

    void resized() override
    {
        const int h = getHeight(), ear = juce::roundToInt (getWidth() * (15.88 / 482.6));
        auto r = getLocalBounds().reduced (ear, 0);
        const int knob = juce::jmax (16, juce::roundToInt (h * 0.58));
        power.setBounds (r.removeFromLeft (juce::roundToInt (h * 0.8)).withSizeKeepingCentre (knob, knob));
        // move and remove: small keys at the right of the face
        auto right = r.removeFromRight (juce::roundToInt (h * 1.6)).reduced (3, juce::jmax (2, h / 10));
        const int key = juce::jmin (right.getHeight(), juce::jmax (14, right.getWidth() / 3 - 2));
        remove.setBounds (right.removeFromRight (key).withSizeKeepingCentre (key, key));
        right.removeFromRight (2);
        down.setBounds (right.removeFromRight (key).withSizeKeepingCentre (key, key));
        right.removeFromRight (2);
        up.setBounds (right.removeFromRight (key).withSizeKeepingCentre (key, key));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.select (slot.uid);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Insert an effect before this");
            m.addItem (2, "Insert an effect after this");
            m.addSeparator();
            m.addItem (3, slot.bypassed.load() ? "Switch on" : "Bypass");
            m.addItem (4, "Remove");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, uid = slot.uid] (int r)
            {
                auto& c = owner.chain();
                int pos = 0;
                for (int i = 0; i < (int) c.slots.size(); ++i) if (c.slots[(size_t) i]->uid == uid) pos = i;
                if (r == 1) owner.showAddMenu (pos);
                if (r == 2) owner.showAddMenu (pos + 1);
                if (r == 3) { slot.bypassed = ! slot.bypassed.load(); power.setToggleState (! slot.bypassed.load(), juce::dontSendNotification); repaint(); owner.changed(); }
                if (r == 4) owner.removeSlot (uid);
            });
        }
    }

private:
    RackPanel& owner;
    Slot& slot;
    juce::Colour colour;
    PowerButton power;
    juce::TextButton up { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")) },
                     down { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")) },
                     remove { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
};

//==============================================================================
class RackPanel::AddButton : public juce::Component
{
public:
    explicit AddButton (RackPanel& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (getWidth() * (15.88f / 482.6f), 1.0f);
        g.setColour (isMouseOver() ? Pequod::Colours::mustardLo.withAlpha (0.5f) : Pequod::Colours::well);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Pequod::Colours::mustardLo);
        const float dash[] = { 5.0f, 4.0f };
        juce::Path p;
        p.addRoundedRectangle (r, 3.0f);
        juce::Path d;
        juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
        g.fillPath (d);
        g.setFont (Pequod::heading (14.0f));
        g.setColour (Pequod::Colours::mustard);
        g.drawText ("+  ADD EFFECT", r, juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition())) owner.showAddMenu ((int) owner.chain().slots.size());
    }

private:
    RackPanel& owner;
};

//==============================================================================
RackPanel::RackPanel (Rack& r) : rack (r)
{
    addButton = std::make_unique<AddButton> (*this);
    units.addAndMakeVisible (*addButton);
    unitView.setViewedComponent (&units, false);
    unitView.setScrollBarsShown (true, false);
    unitView.setScrollBarThickness (8);
    addAndMakeVisible (unitView);

    level.setSliderStyle (juce::Slider::LinearHorizontal);
    level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    level.setRange (-24.0, 12.0, 0.1);
    level.setTextValueSuffix (" dB");
    level.setDoubleClickReturnValue (true, 0.0);
    level.onValueChange = [this]
    {
        auto& c = chain();
        if (shown.kind == ChainKind::track) c.trimDb = (float) level.getValue();
        else c.levelDb = (float) level.getValue();
        if (onChanged) onChanged();
    };
    addAndMakeVisible (level);
    levelLabel.setFont (Pequod::heading (12.0f));
    levelLabel.setColour (juce::Label::textColourId, Pequod::Colours::textDim);
    addAndMakeVisible (levelLabel);

    bypassAll.setClickingTogglesState (true);
    bypassAll.setTooltip ("Bypass every effect on this chain (it keeps its delay, so the tracks stay in time)");
    bypassAll.onClick = [this] { chain().bypassAll = bypassAll.getToggleState(); for (auto* u : unitRows) u->repaint(); changed(); };
    copyButton.setTooltip ("Copy this chain, with every setting");
    pasteButton.setTooltip ("Replace this chain with the one you copied");
    clearButton.setTooltip ("Take every effect off this chain");
    copyButton.onClick  = [this] { copyChain(); };
    pasteButton.onClick = [this] { pasteChain(); };
    clearButton.onClick = [this] { clearChain(); };
    for (auto* b : { &bypassAll, &copyButton, &pasteButton, &clearButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    addAndMakeVisible (editorHolder);
    show ({ ChainKind::track, 0 }, "Track 1");
}

RackPanel::~RackPanel()
{
    closeEditor();
}

void RackPanel::show (ChainRef ref, const juce::String& t)
{
    const bool same = ref == shown && ! unitRows.isEmpty();
    shown = ref;
    title = t;
    if (! same) { closeEditor(); selectedUid = 0; }
    levelLabel.setText (ref.kind == ChainKind::track ? "TRIM" : "LEVEL", juce::dontSendNotification);
    level.setValue (ref.kind == ChainKind::track ? chain().trimDb.load() : chain().levelDb.load(), juce::dontSendNotification);
    bypassAll.setToggleState (chain().bypassAll.load(), juce::dontSendNotification);
    rebuildUnits();
    if (selectedUid == 0 && ! chain().slots.empty()) select (chain().slots.front()->uid);
    repaint();
}

void RackPanel::setTitle (const juce::String& t)
{
    title = t;
    repaint (header);
}

void RackPanel::chainReplaced()
{
    closeEditor();
    selectedUid = 0;
    show (shown, title);
}

void RackPanel::closeEditor()
{
    if (editor != nullptr)
    {
        if (auto* p = editor->getAudioProcessor()) p->editorBeingDeleted (editor.get());
        editor = nullptr;
    }
    editorUid = 0;
}

void RackPanel::rebuildUnits()
{
    unitRows.clear();
    for (auto& s : chain().slots)
        units.addAndMakeVisible (unitRows.add (new Unit (*this, *s)));
    resized();
}

void RackPanel::select (int uid)
{
    selectedUid = uid;
    for (auto* u : unitRows) u->repaint();
    showEditorFor (uid);
}

void RackPanel::showEditorFor (int uid)
{
    if (uid == editorUid && editor != nullptr) return;
    closeEditor();
    for (auto& s : chain().slots)
    {
        if (s->uid != uid || s->processor == nullptr) continue;
        if (auto* e = s->processor->createEditorAndMakeActive())
        {
            editor.reset (e);
            editorUid = uid;
            editorScales = Plugins::isNative (s->identifier);   // a VST3's own window can't be scaled
            editorHolder.addAndMakeVisible (*editor);
            layoutEditor();
        }
    }
    repaint();
}

void RackPanel::layoutEditor()
{
    editorHolder.setBounds (editorArea);
    if (editor == nullptr) return;
    // the panel at its own size, scaled down to fit (never up past 1.25x)
    editor->setTransform ({});
    const int w = juce::jmax (1, editor->getWidth()), h = juce::jmax (1, editor->getHeight());
    const float k = editorScales ? juce::jmin (1.25f, (float) editorArea.getWidth() / (float) w, (float) editorArea.getHeight() / (float) h) : 1.0f;
    const float x = juce::jmax (0.0f, ((float) editorArea.getWidth() - (float) w * k) * 0.5f);
    const float y = juce::jmax (0.0f, juce::jmin (((float) editorArea.getHeight() - (float) h * k) * 0.5f, 12.0f));
    editor->setTopLeftPosition (0, 0);
    editor->setTransform (juce::AffineTransform::scale (k).translated (x, y));
}

void RackPanel::showAddMenu (int insertAt)
{
    juce::PopupMenu m;
    auto& list = Plugins::studio();
    std::vector<const Plugins::Entry*> byId;
    int group = -1;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (group >= 0) m.addSubMenu (StudioPlugins::groups()[(size_t) group].name, sub);
        sub = {};
    };
    for (auto& e : list)
    {
        if (e.group != group) { flush(); group = e.group; }
        byId.push_back (&e);
        sub.addItem ((int) byId.size(), e.name + "   " + e.collection);
    }
    flush();
    if (list.empty())
        m.addItem (-1, "No PEQUOD studio plugins in this build", false);
    m.addSeparator();
    m.addItem (-2, "Look for PEQUOD VST3s (Pink Floyd, Joe Meek, Albini...)");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton.get()).withMinimumWidth (260),
                     [this, byId, insertAt] (int r)
    {
        if (r >= 1 && r <= (int) byId.size()) addPlugin (byId[(size_t) r - 1]->identifier, insertAt);
        if (r == -2 && onScanVst3) onScanVst3();
    });
}

void RackPanel::addPlugin (const juce::String& id, int insertAt)
{
    if (! createPlugin) return;
    juce::String err;
    auto p = createPlugin (id, err);
    if (p == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "EMERSON", "Couldn't load it: " + err);
        return;
    }
    auto s = std::make_unique<Slot>();
    s->uid = rack.nextUid();
    s->identifier = id;
    if (auto* e = Plugins::find (id)) { s->name = e->name; s->collection = e->collection; }
    else s->name = p->getName();
    s->processor = std::move (p);
    rack.prepareSlot (*s, chain());
    const int uid = s->uid;

    auto& slots = chain().slots;
    insertAt = juce::jlimit (0, (int) slots.size(), insertAt);
    slots.insert (slots.begin() + insertAt, std::move (s));
    rack.rebuild();
    rebuildUnits();
    select (uid);
    changed();
}

void RackPanel::removeSlot (int uid)
{
    auto& slots = chain().slots;
    for (auto it = slots.begin(); it != slots.end(); ++it)
    {
        if ((*it)->uid != uid) continue;
        if (editorUid == uid) closeEditor();
        const int pos = (int) (it - slots.begin());
        std::vector<std::unique_ptr<Slot>> removed;
        removed.push_back (std::move (*it));
        slots.erase (it);
        rack.rebuild (std::move (removed));
        rebuildUnits();
        if (selectedUid == uid)
        {
            selectedUid = 0;
            if (! slots.empty()) select (slots[(size_t) juce::jmin (pos, (int) slots.size() - 1)]->uid);
        }
        changed();
        return;
    }
}

void RackPanel::moveSlot (int uid, int delta)
{
    auto& slots = chain().slots;
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        if (slots[(size_t) i]->uid != uid) continue;
        const int j = i + delta;
        if (! juce::isPositiveAndBelow (j, (int) slots.size())) return;
        std::swap (slots[(size_t) i], slots[(size_t) j]);
        rack.rebuild();
        rebuildUnits();
        changed();
        return;
    }
}

void RackPanel::copyChain()
{
    clipboard = RackState::chainToXml (chain());
    pasteButton.setEnabled (true);
}

void RackPanel::pasteChain()
{
    if (clipboard == nullptr || ! createPlugin) return;
    closeEditor();
    juce::StringArray missing;
    std::vector<std::unique_ptr<Slot>> removed;
    RackState::chainFromXml (*clipboard, rack, chain(), createPlugin, missing, removed, false);
    rack.rebuild (std::move (removed));
    chainReplaced();
    changed();
}

void RackPanel::clearChain()
{
    if (chain().slots.empty()) return;
    closeEditor();
    std::vector<std::unique_ptr<Slot>> removed;
    for (auto& s : chain().slots) removed.push_back (std::move (s));
    chain().slots.clear();
    rack.rebuild (std::move (removed));
    chainReplaced();
    changed();
}

void RackPanel::changed()
{
    repaint (header);
    if (onChanged) onChanged();
}

void RackPanel::updateMeters() {}

//==============================================================================
void RackPanel::paint (juce::Graphics& g)
{
    g.fillAll (Pequod::Colours::bg);

    // header: what this chain is
    g.setColour (Pequod::Colours::panel);
    g.fillRect (header);
    g.setColour (Pequod::Colours::mustardLo);
    g.fillRect (header.removeFromBottom (1));
    auto h = header.reduced (14, 0);
    g.setFont (Pequod::heading (20.0f));
    g.setColour (Pequod::Colours::mustard);
    juce::String kind = shown.kind == ChainKind::track ? "TRACK " + juce::String (shown.index + 1) + "  " : juce::String();
    g.drawText (kind + title.toUpperCase(), h.removeFromTop (header.getHeight() / 2 + 4).withTrimmedTop (6), juce::Justification::bottomLeft, true);

    g.setFont (Pequod::body (12.0f));
    g.setColour (Pequod::Colours::textDim);
    juce::String line;
    switch (shown.kind)
    {
        case ChainKind::track:
            line = juce::String (juce::CharPointer_UTF8 ("From Hawthorne \xe2\x86\x92 these inserts, top to bottom \xe2\x86\x92 "))
                 + "WHITMAN's strip " + juce::String (shown.index + 1);
            break;
        case ChainKind::fxReturn:
            line = shown.index == 0
                 ? juce::String (juce::CharPointer_UTF8 ("WHITMAN's ECHO send \xe2\x86\x92 these \xe2\x86\x92 WHITMAN's ECHO RETURN (set WHITMAN's CHAMBER to EMERSON)"))
                 : juce::String (juce::CharPointer_UTF8 ("WHITMAN's send B \xe2\x86\x92 these \xe2\x86\x92 WHITMAN's return B"));
            break;
        case ChainKind::mix:
            line = juce::String (juce::CharPointer_UTF8 ("WHITMAN's stereo mix \xe2\x86\x92 these \xe2\x86\x92 Hawthorne's 2-track master"));
            break;
    }
    const int lat = chain().latency();
    if (lat > 0)
        line << "     delay " << juce::String (1000.0 * lat / juce::jmax (1.0, rack.getSampleRate()), 1) << " ms";
    g.drawText (line, h.withTrimmedBottom (6), juce::Justification::topLeft, true);

    // the units' rack rails
    auto rails = unitView.getBounds().toFloat();
    g.setColour (Pequod::Colours::rail);
    g.fillRect (rails);

    // the panel area
    g.setColour (Pequod::Colours::well);
    g.fillRect (editorArea);
    if (editor == nullptr)
    {
        g.setFont (Pequod::heading (14.0f));
        g.setColour (Pequod::Colours::textDim);
        g.drawText (chain().slots.empty() ? "No effects on this chain yet: + ADD EFFECT" : "Pick an effect to see its panel",
                    editorArea, juce::Justification::centred);
    }
}

void RackPanel::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (58);

    auto bar = r.removeFromTop (40).reduced (10, 7);
    for (auto* b : { &clearButton, &pasteButton, &copyButton })
    {
        b->setBounds (bar.removeFromRight (64));
        bar.removeFromRight (6);
    }
    pasteButton.setEnabled (clipboard != nullptr);
    bar.removeFromRight (6);
    bypassAll.setBounds (bar.removeFromRight (104));
    levelLabel.setBounds (bar.removeFromLeft (52));
    level.setBounds (bar.removeFromLeft (juce::jmin (300, bar.getWidth() - 10)));

    r.removeFromTop (2);
    const int unitsW = unitsWidthFor (r.getWidth());
    unitView.setBounds (r.removeFromLeft (unitsW));
    r.removeFromLeft (8);
    editorArea = r.reduced (0, 0).withTrimmedBottom (8).withTrimmedRight (8);

    const int fullH = unitHeightFor (unitsW) + unitGap;
    const int w = unitsW - (((int) unitRows.size() + 1) * fullH > unitView.getHeight() ? unitView.getScrollBarThickness() : 0);
    const int unitH = unitHeightFor (w);
    int y = 6;
    for (auto* u : unitRows) { u->setBounds (0, y, w, unitH); y += unitH + unitGap; }
    addButton->setBounds (0, y, w, unitH);     // an empty 1U space
    y += unitH + 6;
    units.setSize (w, juce::jmax (y, unitView.getHeight()));
    layoutEditor();
}

} // namespace emerson

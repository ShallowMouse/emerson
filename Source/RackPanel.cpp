#include "RackPanel.h"
#include "Engine/RackState.h"
#include "Plugins.h"
#include "PequodPanel.h"

namespace emerson
{

// The chain is drawn as a 19-inch rack, top to bottom in signal order. Every effect's own panel is
// mounted in it: scaled to the rack's width between the ears and given a whole number of rack
// units (1U = 44.45 mm of a 482.6 mm wide panel, ears included), the space above and below the
// panel's controls a blank plate.
static constexpr double rackWidthMm = 482.6, unitMm = 44.45, earMm = 15.88, holeSpanMm = 31.75;
static constexpr int unitGap = 1;
static int earFor (int width)  { return juce::roundToInt (width * (earMm / rackWidthMm)); }
static double unitFor (int width) { return width * (unitMm / rackWidthMm); }

// PEQUOD's own panels sit on a wood surround (14 px) with a rounded plate: in the rack only the plate
// shows, square to the ears
static int pequodCrop() { return PequodPanel::isRackMounted() ? 0 : 18; }   // (rack-mounted panels have no wood)

//==============================================================================
class RackPanel::Unit : public juce::Component, private juce::ComponentListener
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
        up.setTooltip ("Move up the rack (earlier in the chain)");
        down.setTooltip ("Move down the rack (later in the chain)");
        remove.setTooltip ("Take it out of the rack");
        up.onClick     = [this] { owner.moveSlot (slot.uid, -1); };
        down.onClick   = [this] { owner.moveSlot (slot.uid, +1); };
        remove.onClick = [this] { owner.removeSlot (slot.uid); };

        face.setInterceptsMouseClicks (false, true);
        addAndMakeVisible (face);
        if (slot.processor != nullptr)
            if (auto* e = slot.processor->createEditorAndMakeActive())
            {
                editor.reset (e);
                native = Plugins::isNative (slot.identifier);
                face.addAndMakeVisible (*editor);
                editor->addComponentListener (this);
            }
    }

    ~Unit() override
    {
        if (editor != nullptr)
        {
            editor->removeComponentListener (this);
            if (auto* p = editor->getAudioProcessor()) p->editorBeingDeleted (editor.get());
            editor = nullptr;
        }
    }

    int uid() const { return slot.uid; }

    /** The panel's controls, in its own pixels (PEQUOD's without the wood). */
    juce::Rectangle<int> content() const
    {
        if (editor == nullptr) return {};
        auto r = juce::Rectangle<int> (editor->getWidth(), editor->getHeight());
        return native ? r.reduced (pequodCrop()) : r;
    }

    /** Height in the rack at this width: a whole number of units. */
    int heightFor (int width) const
    {
        const double u = unitFor (width);
        const int faceW = width - 2 * earFor (width);
        int units = 1;
        if (auto c = content(); ! c.isEmpty())
        {
            const double k = native ? (double) faceW / c.getWidth() : 1.0;   // a VST3's window can't be scaled
            units = juce::jmax (1, (int) std::ceil (c.getHeight() * k / u - 0.02));
        }
        return juce::roundToInt (u * units);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float ear = (float) earFor (getWidth());
        // the ears: the panel's own metal, holes on the rails at each end unit
        g.setColour (juce::Colour (0xff2a2b2e));
        g.fillRect (r);
        g.setColour (juce::Colour (0xff3a3b3f));
        g.drawRect (r, 1.0f);
        const float u = (float) unitFor (getWidth());
        const int units = juce::jmax (1, juce::roundToInt (r.getHeight() / u));
        const float half = u * (float) (holeSpanMm / unitMm) * 0.5f, screw = juce::jmax (2.5f, u * 0.055f);
        for (float x : { r.getX() + ear * 0.5f, r.getRight() - ear * 0.5f })
            for (int k : { 0, units - 1 })
            {
                const float cy = r.getY() + u * ((float) k + 0.5f);
                Pequod::drawScrew (g, { x, cy - half }, screw);
                if (units == 1 || k == units - 1) Pequod::drawScrew (g, { x, cy + half }, screw);
            }

        // the face: a blank plate where the panel doesn't reach
        auto f = face.getBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (colour.withMultipliedBrightness (0.5f), f.getX(), f.getY(),
                                                 colour.withMultipliedBrightness (0.3f), f.getX(), f.getBottom(), false));
        g.fillRect (f);
        if (editor == nullptr)
        {
            auto t = f.toNearestInt().reduced (12, 4);
            g.setFont (Pequod::heading (juce::jlimit (12.0f, 18.0f, f.getHeight() * 0.3f)));
            g.setColour (Pequod::Colours::text);
            g.drawText (slot.name.toUpperCase(), t, juce::Justification::centredLeft, true);
            g.setFont (Pequod::body (11.0f));
            g.setColour (Pequod::Colours::text.withAlpha (0.6f));
            g.drawText (slot.collection, t, juce::Justification::centredRight, true);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // bypassed: the face goes dark, like a unit switched off
        if (slot.bypassed.load() || owner.chain().bypassAll.load())
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (face.getBounds());
        }
    }

    void resized() override
    {
        const int ear = earFor (getWidth());
        auto r = getLocalBounds();
        face.setBounds (r.reduced (ear, 0));

        // power and remove on the left ear, move up / down on the right
        const int key = juce::jlimit (12, 24, ear - 6);
        auto left = r.removeFromLeft (ear), right = r.removeFromRight (ear);
        const int mid = juce::roundToInt (unitFor (getWidth()) * 0.5);   // the middle of the top unit
        power.setBounds (juce::Rectangle<int> (key, key).withCentre ({ left.getCentreX(), mid - key / 2 - 2 }));
        remove.setBounds (juce::Rectangle<int> (key, key).withCentre ({ left.getCentreX(), mid + key / 2 + 2 }));
        up.setBounds   (juce::Rectangle<int> (key, key).withCentre ({ right.getCentreX(), mid - key / 2 - 2 }));
        down.setBounds (juce::Rectangle<int> (key, key).withCentre ({ right.getCentreX(), mid + key / 2 + 2 }));

        if (editor != nullptr)
        {
            const auto c = content();
            const float k = native ? (float) face.getWidth() / (float) juce::jmax (1, c.getWidth()) : 1.0f;
            const float x = native ? 0.0f : ((float) face.getWidth() - (float) c.getWidth()) * 0.5f;
            const float y = ((float) face.getHeight() - (float) c.getHeight() * k) * 0.5f;
            editor->setTopLeftPosition (0, 0);
            editor->setTransform (juce::AffineTransform::translation ((float) -c.getX(), (float) -c.getY()).scaled (k).translated (x, y));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addItem (1, "Insert an effect above this");
        m.addItem (2, "Insert an effect below this");
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

private:
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        if (wasResized) juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackPanel> (&owner)] { if (sp != nullptr) sp->resized(); });
    }

    RackPanel& owner;
    Slot& slot;
    juce::Colour colour;
    PowerButton power;
    juce::TextButton up { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")) },
                     down { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")) },
                     remove { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::Component face;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool native = true;
};

//==============================================================================
class RackPanel::AddButton : public juce::Component
{
public:
    explicit AddButton (RackPanel& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced ((float) earFor (getWidth()), 2.0f);
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
    if (! same) { closeEditor(); unitView.setViewPosition (0, 0); }
    levelLabel.setText (ref.kind == ChainKind::track ? "TRIM" : "LEVEL", juce::dontSendNotification);
    level.setValue (ref.kind == ChainKind::track ? chain().trimDb.load() : chain().levelDb.load(), juce::dontSendNotification);
    bypassAll.setToggleState (chain().bypassAll.load(), juce::dontSendNotification);
    rebuildUnits();
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
    show (shown, title);
}

void RackPanel::closeEditor()
{
    unitRows.clear();      // (each unit closes its panel)
}

void RackPanel::rebuildUnits()
{
    // keep the units (and their open panels) that are still in the chain, in the chain's order
    juce::OwnedArray<Unit> old;
    old.swapWith (unitRows);
    for (auto& s : chain().slots)
    {
        Unit* u = nullptr;
        for (int i = 0; i < old.size(); ++i)
            if (old[i]->uid() == s->uid) { u = old.removeAndReturn (i); break; }
        if (u == nullptr) u = new Unit (*this, *s);
        units.addAndMakeVisible (unitRows.add (u));
    }
    old.clear();
    resized();
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
    for (auto* u : unitRows)       // bring the new one into view
        if (u->uid() == uid) unitView.setViewPosition (0, juce::jmax (0, u->getY() - 6));
    changed();
}

void RackPanel::removeSlot (int uid)
{
    auto& slots = chain().slots;
    for (auto it = slots.begin(); it != slots.end(); ++it)
    {
        if ((*it)->uid != uid) continue;
        for (int i = 0; i < unitRows.size(); ++i)          // its panel goes before the plugin
            if (unitRows[i]->uid() == uid) unitRows.remove (i);
        std::vector<std::unique_ptr<Slot>> removed;
        removed.push_back (std::move (*it));
        slots.erase (it);
        rack.rebuild (std::move (removed));
        rebuildUnits();
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

    // the rack: the cabinet behind the rails
    g.setColour (Pequod::Colours::rail);
    g.fillRect (unitView.getBounds());
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
    r = r.withTrimmedBottom (8);
    const int rackW = juce::jmax (200, r.getWidth() - 16 - unitView.getScrollBarThickness());   // (the rack fills the width: no gaps either side)
    unitView.setBounds (r.withSizeKeepingCentre (rackW + unitView.getScrollBarThickness(), r.getHeight()).withY (r.getY()));

    const int w = rackW;
    int y = 6;
    for (auto* u : unitRows) { const int h = u->heightFor (w); u->setBounds (0, y, w, h); y += h + unitGap; }
    addButton->setBounds (0, y, w, juce::roundToInt (unitFor (w)));     // an empty 1U space
    y += addButton->getHeight() + 6;
    units.setSize (w, juce::jmax (y, unitView.getHeight()));
}

} // namespace emerson

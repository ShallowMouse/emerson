#include "TrackList.h"
#include "Melville.h"
#include "EmersonLookAndFeel.h"
#include "Plugins.h"

namespace emerson
{

static const juce::String dot() { return juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")); }

//==============================================================================
class TrackList::Row : public juce::Component
{
public:
    Row (TrackList& o, ChainRef r) : owner (o), ref (r) {}

    Chain& chain() const { return owner.rack.chain (ref.kind, ref.index); }

    juce::String label() const
    {
        switch (ref.kind)
        {
            case ChainKind::track:    return owner.nameOf (ref.index);
            case ChainKind::fxReturn: return ref.index == 0 ? "RETURN A" : "RETURN B";
            case ChainKind::mix:      return "MIX";
        }
        return {};
    }

    juce::String subLabel() const
    {
        if (ref.kind == ChainKind::fxReturn) return "from WHITMAN's " + juce::String (ref.index == 0 ? "ECHO send" : "send B");
        if (ref.kind == ChainKind::mix)      return juce::String (juce::CharPointer_UTF8 ("WHITMAN's mix \xe2\x86\x92 Hawthorne master"));
        return {};
    }

    juce::String inserts() const
    {
        auto& c = chain();
        if (c.slots.empty()) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        juce::StringArray n;
        for (auto& s : c.slots) n.add (s->name);
        return n.joinIntoString (dot());
    }

    juce::Rectangle<int> outBox() const
    {
        return getLocalBounds().removeFromRight (58).reduced (6, 7);
    }

    bool showsOut() const { return ref.kind == ChainKind::track && owner.session.output == Session::Output::directOuts; }

    void paint (juce::Graphics& g) override
    {
        // a strip of the patch panel: its number stamped in, a meter, the name on a strip of tape,
        // what's in its inserts underneath, and a lamp that's lit when it's the one in the rack
        auto r = getLocalBounds();
        const bool sel = owner.selected == ref;
        auto& c = chain();
        const bool off = c.bypassAll.load();

        Pequod::hw::panel (g, r.toFloat(), sel ? juce::Colour (0xff34373c) : juce::Colour (ref.index % 2 == 0 || ref.kind != ChainKind::track ? 0xff25272b : 0xff212327));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (getLocalBounds().removeFromBottom (1));

        // the track's colour down the left edge
        const auto tc = ref.kind == ChainKind::track && owner.colourOf ? owner.colourOf (ref.index) : juce::Colours::transparentBlack;
        auto edge = r.removeFromLeft (5);
        if (! tc.isTransparent()) { g.setColour (tc); g.fillRect (edge.reduced (0, 2)); }

        // number (red when armed), or a lamp for the returns and the mix
        auto num = r.removeFromLeft (30);
        if (ref.kind == ChainKind::track)
        {
            const bool isArmed = juce::isPositiveAndBelow (ref.index, (int) owner.armed.size()) && owner.armed[(size_t) ref.index];
            g.setFont (Pequod::heading (13.0f));
            Pequod::hw::engraved (g, juce::String (ref.index + 1), num.toFloat(), isArmed ? Pequod::Colours::danger : Pequod::Colours::textDim, juce::Justification::centred);
        }
        else
            Pequod::hw::lamp (g, num.toFloat().getCentre(), 3.5f, Pequod::Colours::mustard, ! c.slots.empty() && ! off);

        // meter (after the chain): a ladder of LEDs
        const float pk = c.peakOut.load();
        const float db = juce::Decibels::gainToDecibels (pk, -60.0f);
        Pequod::hw::ledLadder (g, r.removeFromLeft (6).reduced (0, 4).toFloat(), juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f), 8, juce::Colour (0xff60c050));
        r.removeFromLeft (8);

        // the lamp: this is the chain in the rack
        auto lampArea = r.removeFromRight (22);
        Pequod::hw::lamp (g, lampArea.toFloat().getCentre(), 3.5f, juce::Colour (0xffffb040), sel);

        if (showsOut())
        {
            auto ob = outBox().translated (-22, 0);
            r.setRight (ob.getX() - 4);
            const int o = owner.session.directOut[(size_t) ref.index];
            Pequod::hw::key (g, ob.toFloat(), juce::Colour (0xff3a3c40), false, o >= 0, Pequod::Colours::mustardLo);
            g.setColour (o >= 0 ? Pequod::Colours::text : Pequod::Colours::textDim);
            g.setFont (Pequod::heading (11.0f));
            g.drawText (o >= 0 ? "OUT " + juce::String (o + 1) : juce::String ("OUT -"), ob, juce::Justification::centred);
        }

        // the name, written on a strip of tape stuck to the panel
        auto top = r.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (3).toFloat();
        g.setFont (Pequod::heading (13.0f));
        const auto name = label() + (off ? "  [BYPASSED]" : "");
        const float tapeW = juce::jmin (top.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 14.0f);
        auto tape = top.withWidth (tapeW).withTrimmedBottom (1.0f);
        juce::Path strip;
        juce::Random rng (ref.index * 7 + (int) ref.kind * 101 + 3);
        strip.startNewSubPath (tape.getX(), tape.getY());
        strip.lineTo (tape.getRight(), tape.getY());
        for (int k = 1; k <= 5; ++k)          // (torn, not cut)
            strip.lineTo (tape.getRight() + (rng.nextFloat() - 0.5f) * 2.4f, tape.getY() + tape.getHeight() * (float) k / 5.0f);
        strip.lineTo (tape.getX(), tape.getBottom());
        for (int k = 4; k >= 1; --k)
            strip.lineTo (tape.getX() + (rng.nextFloat() - 0.5f) * 2.4f, tape.getY() + tape.getHeight() * (float) k / 5.0f);
        strip.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (strip, juce::AffineTransform::translation (0.6f, 1.0f));
        g.setColour (off ? juce::Colour (0xffa9a291) : juce::Colour (0xffe9e0c7));
        g.fillPath (strip);
        g.setColour (juce::Colour (0xff1c2a52).withAlpha (off ? 0.5f : 0.95f));    // (pen)
        g.drawText (name, tape.withTrimmedLeft (7.0f), juce::Justification::centredLeft, true);

        g.setFont (Pequod::body (11.0f));
        const auto ins = inserts();
        const auto sub = subLabel();
        g.setColour (c.slots.empty() ? Pequod::Colours::textDim.withAlpha (0.6f) : Pequod::Colours::mustard.withAlpha (off ? 0.4f : 0.9f));
        g.drawText (c.slots.empty() && sub.isNotEmpty() ? sub : ins, r.withTrimmedBottom (2).withTrimmedTop (1), juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (showsOut() && outBox().translated (-22, 0).contains (e.getPosition()) && e.mods.isLeftButtonDown())
        {
            if (owner.onPickDirectOut) owner.onPickDirectOut (ref.index);
            return;
        }
        if (owner.onSelect) owner.onSelect (ref);
        if (e.mods.isPopupMenu() && owner.onMenu) owner.onMenu (ref, e.getScreenPosition());
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (ref.kind == ChainKind::track && ! (showsOut() && outBox().translated (-22, 0).contains (e.getPosition())) && owner.onRename)
            owner.onRename (ref.index);
    }

    float shownPeak = -1.0f;

private:
    TrackList& owner;
    ChainRef ref;
};

class TrackList::Rows : public juce::Component {};

//==============================================================================
TrackList::TrackList (Rack& r, Session& s) : rack (r), session (s)
{
    rows = std::make_unique<Rows>();
    viewport.setViewedComponent (rows.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    for (int i = 0; i < Rack::numReturns; ++i)
        addAndMakeVisible (fixedRows.add (new Row (*this, { ChainKind::fxReturn, i })));
    addAndMakeVisible (fixedRows.add (new Row (*this, { ChainKind::mix, 0 })));
}

TrackList::~TrackList() = default;

juce::String TrackList::nameOf (int t) const
{
    if (session.names[t].isNotEmpty() && ! melville::isHosted()) return session.names[t];   // (in MELVILLE the reel's name is the name)
    if (t < hawthorneNames.size() && hawthorneNames[t].isNotEmpty()) return hawthorneNames[t];
    return "Track " + juce::String (t + 1);
}

void TrackList::setNumTracks (int n)
{
    n = juce::jlimit (0, Rack::maxTracks, n);
    if (n == numTracks) return;
    numTracks = n;
    while (trackRows.size() < n)
        rows->addAndMakeVisible (trackRows.add (new Row (*this, { ChainKind::track, trackRows.size() })));
    for (int i = 0; i < trackRows.size(); ++i) trackRows[i]->setVisible (i < n);
    resized();
}

void TrackList::setHawthorneNames (const juce::StringArray& names, const std::vector<bool>& a)
{
    hawthorneNames = names;
    armed = a;
    repaint();
    for (auto* r : trackRows) r->repaint();
}

TrackList::Row* TrackList::rowFor (ChainRef ref) const
{
    if (ref.kind == ChainKind::track) return juce::isPositiveAndBelow (ref.index, trackRows.size()) ? trackRows[ref.index] : nullptr;
    if (ref.kind == ChainKind::fxReturn) return fixedRows[ref.index];
    return fixedRows[Rack::numReturns];
}

void TrackList::setSelected (ChainRef ref)
{
    auto* was = rowFor (selected);
    selected = ref;
    if (was != nullptr) was->repaint();
    if (auto* now = rowFor (ref))
    {
        now->repaint();
        if (ref.kind == ChainKind::track)
        {
            const int y = now->getY(), h = viewport.getViewHeight(), top = viewport.getViewPositionY();
            if (y < top) viewport.setViewPosition (0, y);
            else if (y + rowH > top + h) viewport.setViewPosition (0, y + rowH - h);
        }
    }
}

void TrackList::refresh()
{
    for (auto* r : trackRows) r->repaint();
    for (auto* r : fixedRows) r->repaint();
}

void TrackList::updateMeters()
{
    auto upd = [] (Row* r)
    {
        if (! r->isVisible()) return;
        const float p = r->chain().peakOut.load();
        if (std::abs (p - r->shownPeak) > 0.003f) { r->shownPeak = p; r->repaint (0, 0, 52, r->getHeight()); }
    };
    for (auto* r : trackRows) upd (r);
    for (auto* r : fixedRows) upd (r);
}

void TrackList::resized()
{
    auto r = getLocalBounds();
    auto fixed = r.removeFromBottom (fixedRows.size() * rowH + 10);
    fixed.removeFromTop (10);
    for (auto* row : fixedRows) row->setBounds (fixed.removeFromTop (rowH));

    viewport.setBounds (r);
    rows->setSize (r.getWidth() - (numTracks * rowH > r.getHeight() ? viewport.getScrollBarThickness() : 0), juce::jmax (1, numTracks * rowH));
    for (int i = 0; i < numTracks; ++i) trackRows[i]->setBounds (0, i * rowH, rows->getWidth(), rowH);
}

} // namespace emerson

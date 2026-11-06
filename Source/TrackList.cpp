#include "TrackList.h"
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
        auto r = getLocalBounds();
        const bool sel = owner.selected == ref;
        auto& c = chain();
        const bool off = c.bypassAll.load();

        g.setColour (sel ? Pequod::Colours::panelHi : ((ref.index % 2 == 0 || ref.kind != ChainKind::track) ? Pequod::Colours::panel : Pequod::Colours::panel.darker (0.12f)));
        g.fillRect (r);
        if (sel)
        {
            g.setColour (Pequod::Colours::mustard);
            g.fillRect (r.removeFromLeft (3));
        }
        else r.removeFromLeft (3);
        g.setColour (Pequod::Colours::line.withAlpha (0.6f));
        g.fillRect (getLocalBounds().removeFromBottom (1));

        // number / armed lamp
        auto num = r.removeFromLeft (34);
        if (ref.kind == ChainKind::track)
        {
            const bool isArmed = juce::isPositiveAndBelow (ref.index, (int) owner.armed.size()) && owner.armed[(size_t) ref.index];
            g.setFont (Pequod::heading (13.0f));
            g.setColour (isArmed ? Pequod::Colours::danger : Pequod::Colours::textDim);
            g.drawText (juce::String (ref.index + 1), num, juce::Justification::centred);
        }
        else
        {
            g.setColour (Pequod::Colours::mustardLo);
            g.fillEllipse (num.toFloat().withSizeKeepingCentre (8, 8));
        }

        // meter (after the chain)
        auto meter = r.removeFromLeft (6).reduced (0, 6);
        g.setColour (Pequod::Colours::well);
        g.fillRect (meter);
        const float pk = c.peakOut.load();
        const float db = juce::Decibels::gainToDecibels (pk, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto lit = meter.toFloat();
        lit = lit.removeFromBottom (lit.getHeight() * frac);
        g.setColour (pk >= 1.0f ? Pequod::Colours::danger : (db > -6.0f ? juce::Colour (0xffe0b050) : Pequod::Colours::mustard));
        g.fillRect (lit);
        r.removeFromLeft (8);

        if (showsOut())
        {
            auto ob = outBox();
            r.setRight (ob.getX() - 4);
            const int o = owner.session.directOut[(size_t) ref.index];
            g.setColour (o >= 0 ? Pequod::Colours::mustardLo : Pequod::Colours::well);
            g.fillRoundedRectangle (ob.toFloat(), 3.0f);
            g.setColour (o >= 0 ? Pequod::Colours::text : Pequod::Colours::textDim);
            g.setFont (Pequod::heading (11.0f));
            g.drawText (o >= 0 ? "OUT " + juce::String (o + 1) : juce::String ("OUT -"), ob, juce::Justification::centred);
        }

        // name + inserts
        auto top = r.removeFromTop (r.getHeight() / 2 + 1);
        g.setFont (Pequod::heading (13.0f));
        g.setColour (off ? Pequod::Colours::textDim : Pequod::Colours::text);
        g.drawText (label() + (off ? "  [BYPASSED]" : ""), top.withTrimmedTop (3), juce::Justification::bottomLeft, true);
        g.setFont (Pequod::body (11.0f));
        const auto ins = inserts();
        const auto sub = subLabel();
        g.setColour (c.slots.empty() ? Pequod::Colours::textDim.withAlpha (0.6f) : Pequod::Colours::mustard.withAlpha (off ? 0.4f : 0.9f));
        g.drawText (c.slots.empty() && sub.isNotEmpty() ? sub : ins, r.withTrimmedBottom (3), juce::Justification::topLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (showsOut() && outBox().contains (e.getPosition()) && e.mods.isLeftButtonDown())
        {
            if (owner.onPickDirectOut) owner.onPickDirectOut (ref.index);
            return;
        }
        if (owner.onSelect) owner.onSelect (ref);
        if (e.mods.isPopupMenu() && owner.onMenu) owner.onMenu (ref, e.getScreenPosition());
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (ref.kind == ChainKind::track && ! (showsOut() && outBox().contains (e.getPosition())) && owner.onRename)
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
    if (session.names[t].isNotEmpty()) return session.names[t];
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

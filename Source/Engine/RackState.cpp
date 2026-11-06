#include "RackState.h"

namespace emerson::RackState
{

juce::String kindName (ChainKind k)
{
    switch (k)
    {
        case ChainKind::track:    return "track";
        case ChainKind::fxReturn: return "return";
        case ChainKind::mix:      return "mix";
    }
    return "track";
}

static ChainKind kindFrom (const juce::String& s)
{
    if (s == "return") return ChainKind::fxReturn;
    if (s == "mix") return ChainKind::mix;
    return ChainKind::track;
}

std::unique_ptr<juce::XmlElement> chainToXml (Chain& c)
{
    auto x = std::make_unique<juce::XmlElement> ("CHAIN");
    x->setAttribute ("kind", kindName (c.kind));
    x->setAttribute ("index", c.index);
    x->setAttribute ("trim", (double) c.trimDb.load());
    x->setAttribute ("level", (double) c.levelDb.load());
    x->setAttribute ("bypassAll", c.bypassAll.load());
    for (auto& s : c.slots)
    {
        auto* e = x->createNewChildElement ("SLOT");
        e->setAttribute ("id", s->identifier);
        e->setAttribute ("name", s->name);
        e->setAttribute ("collection", s->collection);
        e->setAttribute ("bypass", s->bypassed.load());
        if (s->processor != nullptr)
        {
            juce::MemoryBlock m;
            s->processor->getStateInformation (m);
            e->setAttribute ("state", m.toBase64Encoding());
        }
    }
    return x;
}

void chainFromXml (const juce::XmlElement& x, Rack& rack, Chain& c, const Loader& load, juce::StringArray& missing,
                   std::vector<std::unique_ptr<Slot>>& removed, bool levels)
{
    for (auto& s : c.slots) removed.push_back (std::move (s));
    c.slots.clear();

    if (levels)
    {
        c.trimDb = (float) x.getDoubleAttribute ("trim", 0.0);
        c.levelDb = (float) x.getDoubleAttribute ("level", 0.0);
        c.bypassAll = x.getBoolAttribute ("bypassAll", false);
    }

    for (auto* e : x.getChildWithTagNameIterator ("SLOT"))
    {
        juce::String err;
        auto p = load (e->getStringAttribute ("id"), err);
        if (p == nullptr)
        {
            missing.addIfNotAlreadyThere (e->getStringAttribute ("name", e->getStringAttribute ("id")));
            continue;
        }
        auto s = std::make_unique<Slot>();
        s->uid = rack.nextUid();
        s->identifier = e->getStringAttribute ("id");
        s->name = e->getStringAttribute ("name", p->getName());
        s->collection = e->getStringAttribute ("collection");
        s->bypassed = e->getBoolAttribute ("bypass", false);
        s->processor = std::move (p);
        rack.prepareSlot (*s, c);
        juce::MemoryBlock m;
        if (m.fromBase64Encoding (e->getStringAttribute ("state")) && m.getSize() > 0)
            s->processor->setStateInformation (m.getData(), (int) m.getSize());
        c.slots.push_back (std::move (s));
    }
}

std::unique_ptr<juce::XmlElement> toXml (Rack& rack, const Session& session)
{
    auto x = std::make_unique<juce::XmlElement> ("EMERSON");
    x->setAttribute ("version", 1);
    x->setAttribute ("output", session.output == Session::Output::directOuts ? "direct" : "whitman");

    for (int t = 0; t < Rack::maxTracks; ++t)
    {
        auto& c = rack.track (t);
        const bool named = session.names[t].isNotEmpty(), patched = session.directOut[(size_t) t] >= 0;
        const bool levels = c.trimDb.load() != 0.0f || c.bypassAll.load();
        if (c.isEmpty() && ! named && ! patched && ! levels) continue;   // nothing to keep
        auto* e = chainToXml (c).release();
        if (named)   e->setAttribute ("name", session.names[t]);
        if (patched) e->setAttribute ("directOut", session.directOut[(size_t) t]);
        x->addChildElement (e);
    }
    for (int i = 0; i < Rack::numReturns; ++i) x->addChildElement (chainToXml (rack.fxReturn (i)).release());
    x->addChildElement (chainToXml (rack.mix()).release());
    return x;
}

void clear (Rack& rack)
{
    std::vector<std::unique_ptr<Slot>> removed;
    auto wipe = [&removed] (Chain& c)
    {
        for (auto& s : c.slots) removed.push_back (std::move (s));
        c.slots.clear();
        c.trimDb = 0.0f;
        c.levelDb = 0.0f;
        c.bypassAll = false;
    };
    for (int t = 0; t < Rack::maxTracks; ++t) wipe (rack.track (t));
    for (int i = 0; i < Rack::numReturns; ++i) wipe (rack.fxReturn (i));
    wipe (rack.mix());
    rack.rebuild (std::move (removed));
}

void fromXml (const juce::XmlElement& x, Rack& rack, Session& session, const Loader& load, juce::StringArray& missing)
{
    if (! x.hasTagName ("EMERSON")) return;
    clear (rack);
    session.output = x.getStringAttribute ("output") == "direct" ? Session::Output::directOuts : Session::Output::whitman;
    std::fill (session.directOut.begin(), session.directOut.end(), -1);
    for (auto& n : session.names) n = {};

    std::vector<std::unique_ptr<Slot>> removed;
    for (auto* e : x.getChildWithTagNameIterator ("CHAIN"))
    {
        const auto k = kindFrom (e->getStringAttribute ("kind"));
        const int i = e->getIntAttribute ("index");
        if (k == ChainKind::track && ! juce::isPositiveAndBelow (i, Rack::maxTracks)) continue;
        if (k == ChainKind::fxReturn && ! juce::isPositiveAndBelow (i, Rack::numReturns)) continue;
        chainFromXml (*e, rack, rack.chain (k, i), load, missing, removed);
        if (k == ChainKind::track)
        {
            session.names.set (i, e->getStringAttribute ("name"));
            session.directOut[(size_t) i] = e->getIntAttribute ("directOut", -1);
        }
    }
    rack.rebuild (std::move (removed));
}

} // namespace emerson::RackState

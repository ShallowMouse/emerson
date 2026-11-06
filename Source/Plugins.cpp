#include "Plugins.h"
#include "EmersonLookAndFeel.h"
#include "PequodNative.h"     // from PEQUOD Host
#include "PluginTags.h"       // from PEQUOD Host

namespace emerson::Plugins
{

static constexpr const char* vst3Prefix = "vst3:";

static juce::AudioPluginFormatManager& formats()
{
    static juce::AudioPluginFormatManager m;
    static const bool ready = []
    {
       #if JUCE_PLUGINHOST_VST3
        m.addFormat (new juce::VST3PluginFormat());
       #endif
        return true;
    }();
    juce::ignoreUnused (ready);
    return m;
}

static juce::KnownPluginList& vst3List()
{
    static juce::KnownPluginList list;
    return list;
}

static std::vector<Entry>& entries()
{
    static std::vector<Entry> v;
    return v;
}
static bool built = false;

static Entry entryFor (const juce::PluginDescription& d, int group, bool native, const juce::String& id)
{
    Entry e;
    e.identifier = id;
    e.name = d.name;
    e.collection = PluginTags::collectionOf (d);
    e.group = group;
    e.colour = juce::Colour (0xff000000 | StudioPlugins::groups()[(size_t) group].rgb);
    e.native = native;
    return e;
}

static void build()
{
    auto& v = entries();
    v.clear();
    for (auto& i : PequodNative::all())
    {
        if (i.instrument) continue;
        const int g = StudioPlugins::groupOf (i.name, i.vendor);
        if (g >= 0) v.push_back (entryFor (PequodNative::describe (i), g, true, PequodNative::idPrefix + i.classId));
    }

    // installed VST3s: only PEQUOD ones the engine doesn't already run
    for (auto& d : vst3List().getTypes())
    {
        if (! d.manufacturerName.trim().startsWithIgnoreCase ("PEQUOD")) continue;
        const int g = StudioPlugins::groupOf (d.name, d.manufacturerName);
        if (g < 0) continue;
        const auto key = StudioPlugins::normalise (d.name) + "|" + StudioPlugins::normalise (d.manufacturerName);
        bool dup = false;
        for (auto& i : PequodNative::all())
            if (StudioPlugins::normalise (i.name) + "|" + StudioPlugins::normalise (i.vendor) == key) dup = true;
        for (auto& e : v)
            if (! e.native && e.name == d.name && e.collection == PluginTags::collectionOf (d)) dup = true;
        if (! dup) v.push_back (entryFor (d, g, false, vst3Prefix + d.createIdentifierString()));
    }

    // in each group: the order the group lists them in, then by collection
    std::stable_sort (v.begin(), v.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.group != b.group) return a.group < b.group;
        auto names = juce::StringArray::fromTokens (StudioPlugins::groups()[(size_t) a.group].plugins, "|", {});
        auto pos = [&names] (const juce::String& n)
        {
            for (int k = 0; k < names.size(); ++k)
                if (StudioPlugins::normalise (names[k]) == StudioPlugins::normalise (n)) return k;
            return 999;
        };
        const int pa = pos (a.name), pb = pos (b.name);
        if (pa != pb) return pa < pb;
        return a.collection < b.collection;
    });
    built = true;
}

const std::vector<Entry>& studio()
{
    if (! built) build();
    return entries();
}

const Entry* find (const juce::String& identifier)
{
    for (auto& e : studio())
        if (e.identifier == identifier) return &e;
    return nullptr;
}

bool isNative (const juce::String& identifier)
{
    return identifier.startsWith (PequodNative::idPrefix);
}

std::unique_ptr<juce::AudioProcessor> create (const juce::String& identifier, juce::String& error)
{
    if (auto* i = PequodNative::find (identifier))
        return std::make_unique<PequodNative::Instance> (*i);

    if (identifier.startsWith (vst3Prefix))
    {
        const auto id = identifier.substring ((int) std::strlen (vst3Prefix));
        if (auto d = vst3List().getTypeForIdentifierString (id))
        {
            juce::String err;
            if (auto p = formats().createPluginInstance (*d, 48000.0, 512, err))
                return p;
            error = err.isNotEmpty() ? err : "the VST3 wouldn't open";
            return nullptr;
        }
        error = "its VST3 isn't installed (or hasn't been found yet: + ADD EFFECT > Look for PEQUOD VST3s)";
        return nullptr;
    }
    error = "not in this version of EMERSON";
    return nullptr;
}

juce::Colour colourOf (const juce::String& identifier)
{
    if (auto* e = find (identifier)) return e->colour;
    return Pequod::Colours::mustard;
}

int scanVst3 (const juce::FileSearchPath& extra)
{
   #if JUCE_PLUGINHOST_VST3
    juce::VST3PluginFormat* vst3 = nullptr;
    for (auto* f : formats().getFormats())
        if (auto* v = dynamic_cast<juce::VST3PluginFormat*> (f)) vst3 = v;
    if (vst3 == nullptr) return 0;

    auto path = vst3->getDefaultLocationsToSearch();
    for (int i = 0; i < extra.getNumPaths(); ++i) path.add (extra[i]);

    // only open files named like a studio plugin: there may be hundreds of other VST3s
    juce::Array<juce::File> files;
    for (int i = 0; i < path.getNumPaths(); ++i)
        for (const auto& e : juce::RangedDirectoryIterator (path[i], true, "*.vst3", juce::File::findFilesAndDirectories))
        {
            const auto f = e.getFile();
            bool insideBundle = false;   // the binary inside a .vst3 bundle folder: the bundle is enough
            for (auto p = f.getParentDirectory(); p != p.getParentDirectory(); p = p.getParentDirectory())
                if (p.getFileExtension().equalsIgnoreCase (".vst3")) { insideBundle = true; break; }
            if (insideBundle) continue;
            if (f.getFileExtension().equalsIgnoreCase (".vst3") && StudioPlugins::offers (f.getFileNameWithoutExtension()))
                files.addIfNotAlreadyThere (f);
        }

    auto& list = vst3List();
    list.clear();
    for (auto& f : files)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        list.scanAndAddFile (f.getFullPathName(), true, found, *vst3);
    }
    // keep PEQUOD's only
    for (auto& d : list.getTypes())
        if (! d.manufacturerName.trim().startsWithIgnoreCase ("PEQUOD"))
            list.removeType (d);
   #endif
    build();
    int n = 0;
    for (auto& e : entries()) if (! e.native) ++n;
    return n;
}

std::unique_ptr<juce::XmlElement> vst3ListToXml()
{
    return vst3List().createXml();
}

void vst3ListFromXml (const juce::XmlElement& x)
{
    vst3List().recreateFromXml (x);
    build();
}

} // namespace emerson::Plugins

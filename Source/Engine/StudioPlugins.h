#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace emerson
{

//==============================================================================
/** Which PEQUOD plugins EMERSON offers: the studio and mixing processors only.
    Amps, cabs, drive pedals, guitar pedals, instruments, mics and tuners stay in
    PEQUOD Host. Names are the plugins' own (as the PEQUOD engine reports them);
    the same name in two collections (Echorec, say) appears under both. */
namespace StudioPlugins
{
    struct Group { const char* name; juce::uint32 rgb; const char* plugins; };

    /** In browser order. The colours are the PEQUOD category colours. */
    inline const std::vector<Group>& groups()
    {
        static const std::vector<Group> list
        {
            { "EQ",                       0x2C4A9A, "Studio EQ|Black Box EQ|Position 4" },
            { "Compressors & limiters",   0xA87A1C, "RS124|Vari-Mu|FET Limiter|Black Box Limiter|Channel Dynamics|Level Loc|Opto Comp|"
                                                    "Barker Bus|Listen Mic|Room Crush|Grit Comp" },
            { "Gates & levellers",        0x66661C, "Smart Gate|Expander|Evenhand" },
            { "Consoles & channel strips",0x46596B, "Valve Desk|TG Channel|TG Strip|V72 V76|Valve Mixer|Gold Star Desk|Neotek Channel|"
                                                    "Plank Console|Ampex 351|Vocal Chain|Croon Vocal|Elfman Vocal|Orchestra Bus|Mystic Horns" },
            { "Tape",                     0x6B4A2E, "Tape Machine|Multitrack Tape|Twin-Track Tape|Studer 169|A800 Bus|Ampex 350|Analog Tape|"
                                                    "Repro Head|Varispeed|Backwards|Tape Loop|Warmer" },
            { "Echo & delay",             0x1E5A84, "Tape Echo|Three-Head Echo|Echorec|RE-301|Copicat|Chorus Echo|Plank Echo|Revox Loop|"
                                                    "Space Station|Ford Space Station|AMS 15-80S|Doubler|Quad Tracker" },
            { "Plates & springs",         0x1F5F63, "Plate 140|EMT Plate|Strawberry Plate|Spring Reverb|Heater Spring|Nonlin|Hall|Soaked" },
            { "Rooms & chambers",         0x1F5E4A, "Echo Chamber|Studio Spaces|Gold Star Chamber|Hollywood Chamber|Water Tank|Floyd Spaces|"
                                                    "Albini Rooms|Antonoff Rooms|Black Box Rooms|blink Rooms|Boingo Rooms|Deltalab Rooms|"
                                                    "Finn Rooms|Ford Rooms|Hannett Rooms|Lanham Rooms|Meek Rooms|Monkeys Rooms|"
                                                    "Montserrat Rooms|Phoenix Rooms|Plank Rooms|Queen Rooms|Sing Sing Rooms|sombr Rooms|"
                                                    "Spector Rooms|Stray Cats Rooms|Strokes Rooms|Townhouse Rooms" },
            { "Studio modulation",        0x5C2E7A, "Tape Flanger|Hand Phaser|Bi-Phase|Time Modulator|Dimension|CE-1 Chorus Ensemble|"
                                                    "Auto-Pan|Rotor|Wall of Sound" },
            { "Character",                0x8C2F62, "Phone Line|Car Radio|Transcription Disc|Cheap Speaker" },
        };
        return list;
    }

    inline juce::String normalise (const juce::String& s)
    {
        return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    }

    /** Index into groups(), or -1 if EMERSON doesn't offer it. */
    inline int groupOf (const juce::String& pluginName, const juce::String& vendor = {})
    {
        if (vendor.trim().equalsIgnoreCase ("PEQUOD Pedals")) return -1;   // the pedal pack: PEQUOD Host's
        const auto key = normalise (pluginName);
        auto& g = groups();
        for (int i = 0; i < (int) g.size(); ++i)
            for (auto& n : juce::StringArray::fromTokens (g[(size_t) i].plugins, "|", {}))
                if (normalise (n) == key)
                    return i;
        return -1;
    }

    inline bool offers (const juce::String& pluginName, const juce::String& vendor = {})
    {
        return groupOf (pluginName, vendor) >= 0;
    }
}

} // namespace emerson

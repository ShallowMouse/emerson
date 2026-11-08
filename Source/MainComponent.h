#pragma once

#include <JuceHeader.h>
#include "PartHeader.h"
#include "Melville.h"
#include "EmersonLookAndFeel.h"
#include "Engine/Rack.h"
#include "Engine/RackState.h"
#include "Engine/RouteMap.h"
#include "Link/LinkHost.h"
#include "RouteStatus.h"
#include "TrackList.h"
#include "RackPanel.h"
#include "Updater.h"

namespace emerson
{

//==============================================================================
/** EMERSON: the FX rack between Hawthorne and WHITMAN (or a hardware desk).

      Hawthorne --Route--> EMERSON (each track's inserts) --Link--> WHITMAN
      Hawthorne <--Route-- EMERSON (mix inserts)          <--Link-- WHITMAN's mix
                           EMERSON FX returns A / B        <-> WHITMAN's sends / returns

    With OUTPUT on DIRECT OUTS the processed tracks go back into Hawthorne instead,
    which plays each one out of the interface output picked for it (to a desk). */
class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    juce::AudioDeviceManager deviceManager;
    juce::ApplicationProperties props;
    bool propsReady = false;
    juce::PropertiesFile& settings();

    Rack rack;
    Session session;
    LinkHost link;
    DirectOutPatch directOutPatch;
    RouteStatus routeStatus;
    RouteStatus::Info routeInfo;
    Updater updater;

    // audio thread
    RouteMap routeMap;
    std::atomic<int> outputMode { 0 };
    std::atomic<int> numTracks { 0 };
    std::atomic<bool> deviceIsRoute { false };
    juce::AudioBuffer<float> trackOut, fromWhitman, returnsOut, mixOut;
    std::vector<const float*> trackInPtrs;
    std::vector<float*> trackOutPtrs;
    std::vector<const float*> toWhitmanPtrs;
    std::vector<float*> fromWhitmanPtrs;
    int blockSize = 512;

    // UI
    TrackList trackList { rack, session };
    RackPanel rackPanel { rack };
    partui::Key audioButton { "Settings", partui::Icon::settings }, openButton { "Open", partui::Icon::open },
                saveButton { "Save", partui::Icon::save }, resetButton { "Reset", partui::Icon::reset };
    juce::TextButton updateButton { "UPDATE" }, toWhitmanButton { "TO WHITMAN" }, directButton { "DIRECT OUTS" };
    /** This part's colour: its own inside MELVILLE, PEQUOD's mustard on its own. */
    juce::Colour partColour() const { return melville::isHosted() ? partui::accent ("emerson") : Pequod::Colours::mustard; }
    juce::Rectangle<int> statusArea, outputLabelArea;
    juce::String deviceName;
    double deviceRate = 0;
    std::atomic<bool> routingChanged { true };
    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dirty = false;
    int saveCountdown = 0, tick = 0;
    bool lastWhitman = false;

    void openAudio();
    void enableAllChannels();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void select (ChainRef);
    void setOutput (Session::Output);
    void publishDirectOuts();
    void pickDirectOut (int track);
    void rename (int track);
    void chainMenu (ChainRef, juce::Point<int>);
    void rackChanged();
    void saveSettings();
    void openRack();
    void saveRack();
    void resetRack();
    void loadRack (const juce::XmlElement&, const juce::String& what);
    void showAudioSettings();
    void applyHawthorneInfo();
    void scanVst3 (bool tell);

    juce::String statusLine (juce::Colour&) const;
    juce::String hintLine() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    int tracksRev = -1;
};

} // namespace emerson

#include "MainComponent.h"
#include "Melville.h"
#include "Plugins.h"
#include "PequodNative.h"
#include "PequodPanel.h"
#include <iostream>

namespace emerson
{

static constexpr const char* routeDeviceName = "Hawthorne Route";
static const juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }
static const juce::String arrow() { return juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")); }

juce::PropertiesFile& MainComponent::settings()
{
    if (! propsReady)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "EMERSON";
        o.filenameSuffix = ".settings";
        o.folderName = "PEQUOD/EMERSON";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
        propsReady = true;
    }
    return *props.getUserSettings();
}

static RackState::Loader loader()
{
    return [] (const juce::String& id, juce::String& err) { return Plugins::create (id, err); };
}

//==============================================================================
MainComponent::MainComponent()
{
    PequodPanel::setRackMounted (true);      // the effects' own panels are rack faceplates (whole units)

    link.open();
    directOutPatch.open();
    if (auto v = settings().getXmlValue ("vst3"))
        Plugins::vst3ListFromXml (*v);

    if (auto x = settings().getXmlValue ("rack"))
    {
        juce::StringArray missing;
        RackState::fromXml (*x, rack, session, loader(), missing);
    }
    currentFile = juce::File (settings().getValue ("file"));

    // diagnostics: EMERSON_LIST_PLUGINS=1 prints every engine plugin and whether EMERSON offers it
    if (juce::SystemStats::getEnvironmentVariable ("EMERSON_LIST_PLUGINS", {}).isNotEmpty())
        for (auto& i : PequodNative::all())
            std::cout << (StudioPlugins::offers (i.name, i.vendor) ? "YES " : "no  ") << i.name << " | " << i.vendor << std::endl;

    // test hook for screenshots: EMERSON_PREVIEW_INSERTS="1=Studio EQ|RS124;5=Vari-Mu;A=Echo Chamber;M=Tape Machine"
    for (auto part : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("EMERSON_PREVIEW_INSERTS", {}), ";", {}))
    {
        const auto where = part.upToFirstOccurrenceOf ("=", false, false).trim();
        auto& c = where == "A" ? rack.fxReturn (0) : where == "B" ? rack.fxReturn (1) : where == "M" ? rack.mix()
                                                   : rack.track (juce::jlimit (1, Rack::maxTracks, where.getIntValue()) - 1);
        for (auto name : juce::StringArray::fromTokens (part.fromFirstOccurrenceOf ("=", false, false), "|", {}))
            for (auto& e : Plugins::studio())
                if (e.name.equalsIgnoreCase (name.trim()))
                {
                    juce::String err;
                    auto s = std::make_unique<Slot>();
                    s->uid = rack.nextUid();
                    s->identifier = e.identifier;
                    s->name = e.name;
                    s->collection = e.collection;
                    s->processor = Plugins::create (e.identifier, err);
                    rack.prepareSlot (*s, c);
                    c.slots.push_back (std::move (s));
                    break;
                }
        rack.rebuild();
    }
    outputMode = (int) session.output;

    for (auto* b : std::initializer_list<juce::TextButton*> { &audioButton, &openButton, &saveButton, &resetButton, &updateButton, &toWhitmanButton, &directButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    for (auto* k : { &audioButton, &openButton, &saveButton, &resetButton }) k->setPartColour (partColour());
    audioButton.setTooltip ("The audio device: pick Hawthorne Route (ASIO)");
    openButton.setTooltip ("Open a saved rack");
    saveButton.setTooltip ("Save this rack (every chain and setting) to a file");
    resetButton.setTooltip ("Take every effect off every chain");
    toWhitmanButton.setTooltip ("The processed tracks go to WHITMAN over the EMERSON Link; WHITMAN's mix comes back through EMERSON to Hawthorne's master");
    directButton.setTooltip ("Each processed track goes back into Hawthorne, which plays it out of the interface output you pick (for a hardware desk)");
    for (auto* b : { &toWhitmanButton, &directButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }
    toWhitmanButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    directButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    (session.output == Session::Output::directOuts ? directButton : toWhitmanButton).setToggleState (true, juce::dontSendNotification);
    // Inside MELVILLE there is one set of track outputs: its patchbay sends them to WHITMAN, to
    // the interface for a hardware desk, or both. So no TO WHITMAN / DIRECT OUTS switch there.
    if (melville::isHosted())
    {
        session.output = Session::Output::whitman;
        outputMode = (int) Session::Output::whitman;
        toWhitmanButton.setVisible (false);
        directButton.setVisible (false);
        audioButton.setTooltip ("Settings: where EMERSON finds PEQUOD VST3s (MELVILLE's INTERFACE picks the audio device)");
        openButton.setVisible (false);      // (the racks are kept with the reel: MELVILLE's LIBRARY)
        saveButton.setVisible (false);
    }
    toWhitmanButton.onClick = [this] { if (toWhitmanButton.getToggleState()) setOutput (Session::Output::whitman); };
    directButton.onClick    = [this] { if (directButton.getToggleState()) setOutput (Session::Output::directOuts); };

    audioButton.onClick = [this]
    {
       #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
        if (melville::isHosted()) { showHostedSettings(); return; }
       #endif
        showAudioSettings();
    };
    openButton.onClick  = [this] { openRack(); };
    saveButton.onClick  = [this] { saveRack(); };
    resetButton.onClick = [this] { resetRack(); };

    updateButton.setVisible (false);
    updateButton.setColour (juce::TextButton::buttonColourId, Pequod::Colours::mustardLo);
    updateButton.onClick = [this]
    {
        if (updater.isDownloading()) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::InfoIcon, "EMERSON " + updater.getLatestVersion(),
            updater.getReleaseNotes() + "\n\nInstall it now? EMERSON closes, updates and starts again.", "INSTALL", "LATER", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) { saveSettings(); updater.install(); } }));
    };
    updater.onChange = [this]
    {
        updateButton.setVisible (updater.isUpdateAvailable());
        updateButton.setButtonText (updater.isDownloading() ? "DOWNLOADING " + juce::String (juce::roundToInt (updater.getProgress() * 100)) + "%"
                                                            : "UPDATE " + updater.getLatestVersion());
        resized();
    };
    updater.onStatus = [] (const juce::String&) {};
    updater.checkSoon();

    trackList.onSelect = [this] (ChainRef r) { select (r); };
    trackList.onMenu = [this] (ChainRef r, juce::Point<int> p) { chainMenu (r, p); };
    trackList.onPickDirectOut = [this] (int t) { pickDirectOut (t); };
    trackList.onRename = [this] (int t) { rename (t); };
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    trackList.colourOf = [] (int t) { auto& tk = melville::tracks(); return tk.colour ? tk.colour (t) : juce::Colours::transparentBlack; };
   #endif
    addAndMakeVisible (trackList);

    rackPanel.createPlugin = [] (const juce::String& id, juce::String& err) { return Plugins::create (id, err); };
    rackPanel.onChanged = [this] { rackChanged(); };
    rackPanel.onScanVst3 = [this]
    {
        // the VST3 folder is always looked in; this adds another (where the PEQUOD collections were unzipped, say)
        const juce::File last (settings().getValue ("vst3Folder"));
        chooser = std::make_unique<juce::FileChooser> ("Where are your PEQUOD VST3s? (the VST3 folder is looked in as well)",
                                                       last.isDirectory() ? last : juce::File());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.isDirectory()) settings().setValue ("vst3Folder", f.getFullPathName());
            scanVst3 (true);
        });
    };
    if (! settings().containsKey ("vst3"))
        juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->scanVst3 (false); });
    addAndMakeVisible (rackPanel);

    setSize (1400, 880);
    openAudio();
    publishDirectOuts();
    select ({ ChainKind::track, 0 });
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    melville::detach (deviceManager);     // (and out of MELVILLE's session)
   #endif
    stopTimer();
    saveSettings();
    rackPanel.closeEditor();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    link.close();
}

//==============================================================================
// Audio

void MainComponent::openAudio()
{
    auto saved = settings().getXmlValue ("audio");
    // inside MELVILLE: MELVILLE's EMERSON device, with the same channels as on its own
    const bool hosted = melville::attach (deviceManager, "emerson");
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    melville::addToSession (deviceManager, {
        [this] { return RackState::toXml (rack, session); },
        [this] (const juce::XmlElement& x) { loadRack (x, "the session"); } });
   #endif
    if (! hosted)
        deviceManager.initialise (Rack::maxTracks + 3, Rack::maxTracks + 2, saved.get(), true);

    if (saved == nullptr && ! hosted)
    {
        // first run: Hawthorne Route if it's installed
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            if (type->getDeviceNames (true).contains (routeDeviceName))
            {
                deviceManager.setCurrentAudioDeviceType (type->getTypeName(), true);
                auto setup = deviceManager.getAudioDeviceSetup();
                setup.inputDeviceName = setup.outputDeviceName = routeDeviceName;
                deviceManager.setAudioDeviceSetup (setup, true);
                break;
            }
        }
    }

    enableAllChannels();
    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (this);
}

void MainComponent::enableAllChannels()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr || ! dev->getName().containsIgnoreCase (routeDeviceName)) return;

    const int ins = dev->getInputChannelNames().size(), outs = dev->getOutputChannelNames().size();
    if (dev->getActiveInputChannels().countNumberOfSetBits() >= ins
        && dev->getActiveOutputChannels().countNumberOfSetBits() >= outs)
        return;

    // the driver shows as many tracks as the reel has; when that grows, switch the new ones on too
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, ins, true);
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, outs, true);
    deviceManager.setAudioDeviceSetup (setup, true);
}

void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    auto m = RouteMap::make (dev->getInputChannelNames(), dev->getActiveInputChannels(),
                             dev->getOutputChannelNames(), dev->getActiveOutputChannels());
    if (! m.isHawthorneRoute)
    {
        // another device (trying it out): its inputs are tracks 1, 2, 3..., the mix goes to outputs 1-2
        const int ins = dev->getActiveInputChannels().countNumberOfSetBits();
        for (int i = 0; i < juce::jmin (ins, RouteMap::maxTracks); ++i) m.trackIn[(size_t) i] = i;
        m.numTracks = juce::jmin (ins, RouteMap::maxTracks);
        const int outs = dev->getActiveOutputChannels().countNumberOfSetBits();
        m.masterOut[0] = outs > 0 ? 0 : -1;
        m.masterOut[1] = outs > 1 ? 1 : -1;
    }
    routeMap = m;
    numTracks = m.numTracks;
    deviceIsRoute = m.isHawthorneRoute;

    blockSize = juce::jlimit (16, link::maxBlock, dev->getCurrentBufferSizeSamples());
    const double sr = dev->getCurrentSampleRate();
    rack.prepare (sr, blockSize);

    trackOut.setSize (Rack::maxTracks, blockSize);
    fromWhitman.setSize (link::fromWhitmanChannels, blockSize);
    returnsOut.setSize (2 * Rack::numReturns, blockSize);
    mixOut.setSize (2, blockSize);
    trackInPtrs.assign ((size_t) Rack::maxTracks, nullptr);
    trackOutPtrs.assign ((size_t) Rack::maxTracks, nullptr);
    toWhitmanPtrs.assign ((size_t) link::toWhitmanChannels, nullptr);
    fromWhitmanPtrs.assign ((size_t) link::fromWhitmanChannels, nullptr);

    link.start ((int) sr, blockSize, m.numTracks);
    routingChanged = true;
}

void MainComponent::audioDeviceStopped()
{
    link.stop();
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                       int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    const int tracks = numTracks.load (std::memory_order_relaxed);
    const bool toWhitman = outputMode.load (std::memory_order_relaxed) == (int) Session::Output::whitman;
    const auto& m = routeMap;

    for (int offset = 0; offset < n; offset += blockSize)
    {
        const int chunk = juce::jmin (blockSize, n - offset);

        for (int c = 0; c < link::fromWhitmanChannels; ++c) fromWhitmanPtrs[(size_t) c] = fromWhitman.getWritePointer (c);
        link.readFromWhitman (fromWhitmanPtrs.data(), chunk);

        for (int t = 0; t < tracks; ++t)
        {
            const int i = m.trackIn[(size_t) t];
            trackInPtrs[(size_t) t] = juce::isPositiveAndBelow (i, numIn) && in[i] != nullptr ? in[i] + offset : nullptr;
            trackOutPtrs[(size_t) t] = trackOut.getWritePointer (t);
        }

        Rack::IO io;
        io.numTracks = tracks;
        io.trackIn = trackInPtrs.data();
        io.trackOut = trackOutPtrs.data();
        for (int r = 0; r < Rack::numReturns; ++r)
            for (int s = 0; s < 2; ++s)
            {
                io.sends[r][s] = toWhitman ? fromWhitman.getReadPointer (link::fromSendA + 2 * r + s) : nullptr;
                io.returnsOut[r][s] = returnsOut.getWritePointer (2 * r + s);
            }
        io.mixIn[0] = toWhitman ? fromWhitman.getReadPointer (link::fromMixLeft) : nullptr;
        io.mixIn[1] = toWhitman ? fromWhitman.getReadPointer (link::fromMixRight) : nullptr;
        io.mixOut[0] = mixOut.getWritePointer (0);
        io.mixOut[1] = mixOut.getWritePointer (1);
        io.numSamples = chunk;
        rack.process (io);

        // to WHITMAN (in DIRECT OUTS too, so its meters still move if it's open)
        for (int t = 0; t < link::maxTracks; ++t) toWhitmanPtrs[(size_t) t] = t < tracks ? trackOut.getReadPointer (t) : nullptr;
        for (int c = 0; c < 2 * Rack::numReturns; ++c) toWhitmanPtrs[(size_t) (link::toReturnA + c)] = returnsOut.getReadPointer (c);
        link.writeToWhitman (toWhitmanPtrs.data(), tracks, chunk);

        if (toWhitman)
        {
            for (int s = 0; s < 2; ++s)
                if (juce::isPositiveAndBelow (m.masterOut[s], numOut) && out[m.masterOut[s]] != nullptr)
                    juce::FloatVectorOperations::copy (out[m.masterOut[s]] + offset, mixOut.getReadPointer (s), chunk);
        }
        else
        {
            for (int t = 0; t < tracks; ++t)
            {
                const int o = m.trackOut[(size_t) t];
                if (juce::isPositiveAndBelow (o, numOut) && out[o] != nullptr)
                    juce::FloatVectorOperations::copy (out[o] + offset, trackOut.getReadPointer (t), chunk);
            }
        }
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    enableAllChannels();
    routingChanged = true;
    if (auto state = melville::isHosted() ? nullptr : deviceManager.createStateXml())
        settings().setValue ("audio", state.get());
}

//==============================================================================
// Chains

void MainComponent::select (ChainRef r)
{
    trackList.setSelected (r);
    rackPanel.show (r, r.kind == ChainKind::track ? trackList.nameOf (r.index)
                     : r.kind == ChainKind::mix ? juce::String ("MIX") : juce::String (r.index == 0 ? "RETURN A" : "RETURN B"));
}

void MainComponent::rackChanged()
{
    trackList.refresh();
    dirty = true;
    saveCountdown = 30;
    repaint (statusArea);
}

void MainComponent::setOutput (Session::Output o)
{
    if (melville::isHosted()) o = Session::Output::whitman;   // (MELVILLE's patchbay does direct outs)
    session.output = o;
    outputMode = (int) o;
    publishDirectOuts();
    trackList.resized();
    trackList.refresh();
    rackChanged();
    repaint();
}

void MainComponent::publishDirectOuts()
{
    directOutPatch.publish (session.output == Session::Output::directOuts, numTracks.load(), session.directOut.data());
}

void MainComponent::pickDirectOut (int t)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Track " + juce::String (t + 1) + " direct out (Hawthorne's interface)");
    m.addItem (1000, "None", true, session.directOut[(size_t) t] < 0);
    juce::PopupMenu more;
    for (int o = 0; o < 64; ++o)
        (o < 32 ? m : more).addItem (o + 1, "Out " + juce::String (o + 1), true, session.directOut[(size_t) t] == o);
    m.addSubMenu ("Out 33-64", more);
    m.addSeparator();
    m.addItem (2000, "From this track down: one output each, starting here");
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, t] (int r)
    {
        if (r == 0) return;
        if (r == 1000) session.directOut[(size_t) t] = -1;
        else if (r == 2000)
        {
            int first = session.directOut[(size_t) t] >= 0 ? session.directOut[(size_t) t] : 0;
            for (int k = t; k < numTracks.load(); ++k) session.directOut[(size_t) k] = first++;
        }
        else session.directOut[(size_t) t] = r - 1;
        publishDirectOuts();
        rackChanged();
    });
}

void MainComponent::rename (int t)
{
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    if (melville::isHosted())
    {
        // one name for the track everywhere: it's kept on Hawthorne's reel
        auto& tk = melville::tracks();
        if (! tk.rename) return;
        auto* w = new juce::AlertWindow ("Track " + juce::String (t + 1), "Its name (on the tape, the rack, the desk and the patchbay)", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", trackList.nameOf (t));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([t, w] (int r)
        {
            if (r == 1) if (auto& f = melville::tracks().rename) f (t, w->getTextEditorContents ("name"));
        }), true);
        return;
    }
   #endif
    auto* w = new juce::AlertWindow ("Track " + juce::String (t + 1), "Its name in EMERSON (leave it empty to use Hawthorne's)", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", session.names[t]);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, t, w] (int r)
    {
        if (r == 1)
        {
            session.names.set (t, w->getTextEditorContents ("name").trim());
            if (rackPanel.getShown() == ChainRef { ChainKind::track, t }) rackPanel.setTitle (trackList.nameOf (t));
            rackChanged();
        }
    }), true);
}

void MainComponent::chainMenu (ChainRef r, juce::Point<int>)
{
    auto& c = rack.chain (r.kind, r.index);
    juce::PopupMenu m;
    m.addItem (1, "Copy chain", ! c.slots.empty());
    m.addItem (2, "Paste chain", rackPanel.hasClipboard());
    m.addItem (3, "Clear chain", ! c.slots.empty());
    m.addItem (4, c.bypassAll.load() ? "Switch chain back on" : "Bypass chain");
    if (r.kind == ChainKind::track)
    {
        m.addSeparator();
        m.addItem (5, "Rename...");
       #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
        if (auto& tk = melville::tracks(); tk.palette != nullptr && tk.recolour != nullptr)
        {
            juce::PopupMenu colours;
            const auto list = tk.palette();
            for (int i = 0; i < list.size(); ++i) colours.addColouredItem (300 + i, "  ", list[i]);
            colours.addItem (399, "None");
            m.addSubMenu ("Colour", colours);
        }
       #endif
        m.addItem (6, "Paste chain to every track", rackPanel.hasClipboard());
        if (session.output == Session::Output::directOuts) m.addItem (7, "Direct out...");
    }
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, r] (int k)
    {
       #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
        if (k >= 300 && k <= 399 && r.kind == ChainKind::track)
        {
            auto& tk = melville::tracks();
            const auto list = tk.palette ? tk.palette() : juce::Array<juce::Colour>();
            if (tk.recolour) tk.recolour (r.index, k == 399 ? juce::Colours::transparentBlack : list[k - 300]);
            return;
        }
       #endif
        auto& ch = rack.chain (r.kind, r.index);
        switch (k)
        {
            case 1: rackPanel.copyChain(); break;
            case 2: rackPanel.pasteChain(); break;
            case 3: rackPanel.clearChain(); break;
            case 4: ch.bypassAll = ! ch.bypassAll.load(); select (r); rackChanged(); break;
            case 5: rename (r.index); break;
            case 6:
                for (int t = 0; t < numTracks.load(); ++t)
                {
                    select ({ ChainKind::track, t });
                    rackPanel.pasteChain();
                }
                select (r);
                break;
            case 7: pickDirectOut (r.index); break;
            default: break;
        }
    });
}

//==============================================================================
// Saving

void MainComponent::saveSettings()
{
    if (auto x = RackState::toXml (rack, session))
        settings().setValue ("rack", x.get());
    settings().setValue ("file", currentFile.getFullPathName());
    if (auto state = melville::isHosted() ? nullptr : deviceManager.createStateXml())
        settings().setValue ("audio", state.get());
    settings().saveIfNeeded();
    dirty = false;
}

void MainComponent::loadRack (const juce::XmlElement& x, const juce::String& what)
{
    rackPanel.closeEditor();
    juce::StringArray missing;
    RackState::fromXml (x, rack, session, loader(), missing);
    if (melville::isHosted()) session.output = Session::Output::whitman;
    outputMode = (int) session.output;
    (session.output == Session::Output::directOuts ? directButton : toWhitmanButton).setToggleState (true, juce::dontSendNotification);
    publishDirectOuts();
    rackPanel.chainReplaced();
    select (rackPanel.getShown());
    trackList.resized();
    rackChanged();
    if (! missing.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "EMERSON",
            what + " uses plugins this EMERSON doesn't have, so they were left out:\n\n" + missing.joinIntoString ("\n"));
}

void MainComponent::openRack()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a rack", currentFile.existsAsFile() ? currentFile : juce::File(), "*.emerson");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr || ! x->hasTagName ("EMERSON"))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "EMERSON", f.getFileName() + " isn't an EMERSON rack.");
            return;
        }
        currentFile = f;
        loadRack (*x, f.getFileNameWithoutExtension());
        saveSettings();
    });
}

void MainComponent::saveRack()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the rack", currentFile.existsAsFile() ? currentFile : juce::File(), "*.emerson");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension ("emerson");
        if (auto x = RackState::toXml (rack, session))
            if (x->writeTo (f))
            {
                currentFile = f;
                saveSettings();
                repaint();
                return;
            }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "EMERSON", "Couldn't save " + f.getFullPathName());
    });
}

void MainComponent::resetRack()
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "RESET",
        "Take every effect off every track, the returns and the mix?", "RESET", "CANCEL", this,
        juce::ModalCallbackFunction::create ([this] (int r)
        {
            if (r != 1) return;
            rackPanel.closeEditor();
            RackState::clear (rack);
            currentFile = juce::File();
            rackPanel.chainReplaced();
            rackChanged();
            saveSettings();
        }));
}

//==============================================================================
// Settings

void MainComponent::showAudioSettings()
{
    auto* sel = new juce::AudioDeviceSelectorComponent (deviceManager, 0, Rack::maxTracks + 3, 0, Rack::maxTracks + 2, false, false, false, false);
    sel->setSize (560, 420);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = "EMERSON audio: pick Hawthorne Route (ASIO)";
    o.dialogBackgroundColour = Pequod::Colours::bg;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

#if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
/** Inside MELVILLE: EMERSON's own settings (the device is MELVILLE's). */
void MainComponent::showHostedSettings()
{
    struct Panel : juce::Component
    {
        explicit Panel (MainComponent& m) : owner (m)
        {
            for (auto* b : { &choose, &again })
            {
                b->setColour (juce::TextButton::buttonColourId, Pequod::Colours::panelHi);
                addAndMakeVisible (b);
            }
            choose.onClick = [this] { if (owner.rackPanel.onScanVst3) owner.rackPanel.onScanVst3(); juce::Timer::callAfterDelay (500, [sp = juce::Component::SafePointer<Panel> (this)] { if (sp != nullptr) sp->repaint(); }); };
            again.onClick  = [this] { owner.scanVst3 (true); };
            setSize (520, 190);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Pequod::Colours::bg);
            auto r = getLocalBounds().reduced (18, 14);
            g.setFont (Pequod::heading (13.0f));
            g.setColour (partui::accent ("emerson"));
            g.drawText ("PEQUOD VST3s", r.removeFromTop (20), juce::Justification::centredLeft);
            g.setFont (Pequod::body (12.5f));
            g.setColour (Pequod::Colours::textDim);
            g.drawFittedText ("The built-in PEQUOD effects are always in + ADD EFFECT. PEQUOD studio VST3s the engine doesn't have "
                              "are found in the VST3 folder and in this one:", r.removeFromTop (36), juce::Justification::topLeft, 2);
            const juce::File f (owner.settings().getValue ("vst3Folder"));
            g.setColour (Pequod::Colours::text);
            g.drawText (f.isDirectory() ? f.getFullPathName() : juce::String ("(none chosen)"), r.removeFromTop (22), juce::Justification::centredLeft, true);
            g.setColour (Pequod::Colours::textDim);
            g.drawText ("The audio device is MELVILLE's: INTERFACE on its bar.", getLocalBounds().reduced (18, 12).removeFromBottom (18), juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (18, 14).withTrimmedTop (82).removeFromTop (30);
            choose.setBounds (r.removeFromLeft (170));
            r.removeFromLeft (8);
            again.setBounds (r.removeFromLeft (130));
        }
        MainComponent& owner;
        juce::TextButton choose { "CHOOSE FOLDER..." }, again { "LOOK AGAIN" };
    };
    melville::showPanel (std::make_unique<Panel> (*this), "EMERSON settings", "emerson");
}
#endif

void MainComponent::scanVst3 (bool tell)
{
    juce::MouseCursor::showWaitCursor();
    juce::FileSearchPath extra;
    if (const juce::File f (settings().getValue ("vst3Folder")); f.isDirectory()) extra.add (f);
    const int n = Plugins::scanVst3 (extra);
    juce::MouseCursor::hideWaitCursor();
    if (auto x = Plugins::vst3ListToXml()) settings().setValue ("vst3", x.get());
    settings().saveIfNeeded();
    if (tell)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "EMERSON",
            n > 0 ? "Found " + juce::String (n) + " PEQUOD studio VST3" + (n == 1 ? "" : "s") + " the built-in engine doesn't have. They're in + ADD EFFECT now."
                  : juce::String ("No other PEQUOD studio VST3s found (in the VST3 folder or the one you picked)."));
}

void MainComponent::applyHawthorneInfo()
{
    const bool use = deviceIsRoute.load() && routeInfo.hawthorneRunning && routeInfo.hasInfo;
    if (use) trackList.setHawthorneNames (routeInfo.trackNames, routeInfo.armed);
    else trackList.setHawthorneNames ({}, {});

    // test hook for screenshots without a sound card: EMERSON_PREVIEW_TRACKS="Kick,Snare,*Vox"
    const auto preview = juce::SystemStats::getEnvironmentVariable ("EMERSON_PREVIEW_TRACKS", {});
    if (deviceManager.getCurrentAudioDevice() == nullptr && preview.isNotEmpty())
    {
        juce::StringArray n;
        std::vector<bool> a;
        for (auto s : juce::StringArray::fromTokens (preview, ",", {}))
        {
            a.push_back (s.startsWith ("*"));
            n.add (s.trimCharactersAtStart ("*"));
        }
        trackList.setNumTracks (n.size());
        trackList.setHawthorneNames (n, a);
    }
    if (rackPanel.getShown().kind == ChainKind::track)
        rackPanel.setTitle (trackList.nameOf (rackPanel.getShown().index));
}

void MainComponent::timerCallback()
{
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    if (const int rev = melville::tracks().rev.load(); rev != tracksRev)     // a track renamed or coloured (on the reel)
    {
        tracksRev = rev;
        trackList.repaint();
        if (rackPanel.getShown().kind == ChainKind::track) rackPanel.setTitle (trackList.nameOf (rackPanel.getShown().index));
    }
   #endif
    if (routingChanged.exchange (false))
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        deviceName = dev != nullptr ? dev->getName() : juce::String();
        deviceRate = dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
        trackList.setNumTracks (dev != nullptr ? numTracks.load() : 0);
        link.setNumTracks (numTracks.load());
        publishDirectOuts();
        applyHawthorneInfo();
        repaint();
    }

    trackList.updateMeters();
    rack.housekeeping();

    if (saveCountdown > 0 && --saveCountdown == 0 && dirty)
        saveSettings();

    if (++tick % 8 == 0)
    {
        if (tick % 16 == 0) enableAllChannels();
        auto now = routeStatus.poll();
        if (! now.sameAs (routeInfo))
        {
            routeInfo = std::move (now);
            applyHawthorneInfo();
            repaint (statusArea);
        }
        const bool w = link.isWhitmanConnected();
        if (w != lastWhitman) { lastWhitman = w; repaint (statusArea); }
        if (tick % 32 == 0) repaint (statusArea);
    }
}

//==============================================================================
// Drawing

juce::String MainComponent::statusLine (juce::Colour& c) const
{
    if (deviceName.isEmpty())
    {
        c = Pequod::Colours::danger;
        return "NO AUDIO DEVICE" + dot() + "click SETTINGS and pick Hawthorne Route (ASIO)";
    }
    const auto rate = juce::String (deviceRate / 1000.0, 1) + " kHz";
    const auto ms = [this] (int samples) { return juce::String (1000.0 * samples / juce::jmax (1.0, deviceRate), 1) + " ms"; };
    juce::String tail;
    if (rack.trackLatency() > 0) tail << dot() << "TRACK DELAY " << ms (rack.trackLatency());

    if (! deviceIsRoute.load())
    {
        c = Pequod::Colours::textDim;
        return deviceName.toUpperCase() + dot() + rate + dot() + "not Hawthorne Route: its inputs are tracks, the mix goes to outputs 1-2" + tail;
    }
   #if JUCE_WINDOWS
    if (! routeInfo.hawthorneRunning)
    {
        c = Pequod::Colours::danger;
        return "HAWTHORNE ROUTE" + dot() + "Hawthorne isn't running: start it and the tracks come in";
    }
   #endif
    const auto reel = routeInfo.hasInfo && routeInfo.reelName.isNotEmpty() ? "REEL: " + routeInfo.reelName.toUpperCase() + dot() : juce::String();
    if (melville::isHosted())
    {
        c = partColour();
        return reel + juce::String (numTracks.load()) + " TRACKS" + dot() + rate + tail;
    }
    const auto head = "HAWTHORNE ROUTE" + dot() + reel + juce::String (numTracks.load()) + " TRACKS" + dot() + rate;

    if (session.output == Session::Output::directOuts)
    {
        int patched = 0;
        for (int t = 0; t < numTracks.load(); ++t) if (session.directOut[(size_t) t] >= 0) ++patched;
        c = patched > 0 ? Pequod::Colours::mustard : juce::Colour (0xffe0b050);
        return head + dot() + "DIRECT OUTS: " + juce::String (patched) + " TRACK" + (patched == 1 ? "" : "S") + " PATCHED" + tail;
    }
    if (! link.isWhitmanConnected())
    {
        c = juce::Colour (0xffe0b050);
        return head + dot() + "WAITING FOR WHITMAN" + tail;
    }
    c = Pequod::Colours::mustard;
    return head + dot() + "WHITMAN CONNECTED" + dot() + "MIX BACK " + ms (blockSize + rack.mix().latency()) + tail;
}

juce::String MainComponent::hintLine() const
{
    if (melville::isHosted())
        return "Inserts on Hawthorne's tracks, WHITMAN's returns and the mix. Their jacks are on MELVILLE's PATCHBAY";
    if (! deviceIsRoute.load())
        return "Hawthorne Route is the ASIO driver Hawthorne installs (Hawthorne > SETTINGS)";
    if (session.output == Session::Output::directOuts)
        return "Click a track's OUT box to pick its interface output; Hawthorne plays it out there (needs a Hawthorne with EMERSON direct outs)";
    if (melville::isHosted())
        return "MELVILLE's PATCHBAY sends the tracks to WHITMAN, the interface or both. Hawthorne's master L / R inputs: ROUTE master L / R";
    if (! link.isWhitmanConnected())
        return "In WHITMAN: SETTINGS > device type EMERSON Link, device EMERSON. Hawthorne's master L / R inputs: ROUTE master L / R";
    return "Hawthorne's ROUTING: master L / R inputs from ROUTE master L / R, and each track's MIX off";
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Pequod::Colours::bg);

    auto bar = getLocalBounds().removeFromTop (partui::headerHeight);
    partui::drawBar (g, bar, partColour());
    partui::drawWordmark (g, bar.reduced (18, 0).removeFromLeft (190), "EMERSON",
                          melville::isHosted() ? juce::String ("FX RACK")
                                               : "PEQUOD  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  FX RACK  " + juce::String (JUCE_APPLICATION_VERSION_STRING),
                          partColour(), Pequod::heading (30.0f));

    juce::Colour c;
    const auto status = statusLine (c);
    g.setFont (Pequod::heading (13.0f));
    g.setColour (c);
    g.drawText (status, statusArea.withHeight (statusArea.getHeight() / 2).translated (0, 4), juce::Justification::centredLeft, true);
    g.setFont (Pequod::body (12.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText (hintLine() + (currentFile.existsAsFile() ? "     [" + currentFile.getFileNameWithoutExtension() + "]" : juce::String()),
                statusArea.withTrimmedTop (statusArea.getHeight() / 2).translated (0, -2), juce::Justification::centredLeft, true);

    g.setFont (Pequod::heading (11.0f));
    g.setColour (Pequod::Colours::textDim);
    if (toWhitmanButton.isVisible())
        g.drawText ("OUTPUT", outputLabelArea, juce::Justification::centredRight);

    // the track list's frame
    g.setColour (Pequod::Colours::rail);
    g.fillRect (trackList.getBounds().expanded (0, 0).withTrimmedTop (-24));
    g.setFont (Pequod::heading (12.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText (session.output == Session::Output::directOuts ? "TRACKS" + arrow() + "INSERTS" + arrow() + "DIRECT OUT" : "TRACKS" + arrow() + "INSERTS" + arrow() + "WHITMAN",
                trackList.getBounds().withY (trackList.getY() - 24).withHeight (24).reduced (12, 0), juce::Justification::centredLeft);

    if (trackList.getNumTracks() == 0)
    {
        g.setFont (Pequod::heading (13.0f));
        g.drawText (deviceName.isEmpty() ? "No audio device" : "No tracks", trackList.getBounds().withHeight (80), juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (partui::headerHeight).withTrimmedBottom (2).reduced (14, 0);
    bar.removeFromLeft (200);
    bar = partui::placeKeys (bar, { &audioButton, &saveButton, &openButton, &resetButton });   // (the keys every part has, right)
    bar.removeFromRight (10);
    directButton.setBounds (bar.removeFromRight (104).withSizeKeepingCentre (104, partui::keyHeight));
    toWhitmanButton.setBounds (bar.removeFromRight (104).withSizeKeepingCentre (104, partui::keyHeight));
    outputLabelArea = bar.removeFromRight (60).withTrimmedRight (8);
    bar.removeFromRight (6);
    if (updateButton.isVisible())
    {
        updateButton.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, partui::keyHeight));
        bar.removeFromRight (6);
    }
    statusArea = bar.withTrimmedRight (10).withSizeKeepingCentre (bar.getWidth() - 10, 40);

    r.removeFromTop (8);
    auto left = r.removeFromLeft (340).withTrimmedLeft (8).withTrimmedBottom (8);
    left.removeFromTop (24);
    trackList.setBounds (left);
    r.removeFromLeft (8);
    rackPanel.setBounds (r.withTrimmedRight (0));
}

} // namespace emerson

#include "MainComponent.h"

class EmersonApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // Test hook: quit automatically after N ms (automated smoke tests).
        auto quitAfter = juce::SystemStats::getEnvironmentVariable ("EMERSON_QUIT_AFTER_MS", {}).getIntValue();
        if (quitAfter > 0)
            juce::Timer::callAfterDelay (quitAfter, [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (mainWindow != nullptr) mainWindow->toFront (true); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, Pequod::Colours::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new emerson::MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 8000, 6000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    EmersonLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (EmersonApplication)

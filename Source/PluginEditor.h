#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

/**
    Editor UI:
      - Large, high-contrast Play / Pause / Stop buttons sized for finger
        use in low-light live performance conditions.
      - A track list where each row doubles as a timeline lane: it shows
        that file's waveform (via AudioThumbnail), and clicking/dragging
        anywhere on a lane seeks ALL loaded tracks to that time (since every
        lane shares the same time-to-pixel scale and they're stacked with
        no gaps, the individual playheads line up into one continuous
        timeline cursor).
      - Each row also shows and lets you change which physical output(s)
        that file is routed to, via a single "Output" dropdown: a mono file
        picks one output (1, 2, 3...), a stereo file picks an adjacent pair
        (1+2, 3+4, ...), defaulting to a sensible free output/pair assigned
        automatically when the file is loaded.
      - Each row has a volume slider spanning the row's full right-hand
        column width, with the mute/output/delete controls below it.
*/
class TrackDeckAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::ListBoxModel,
                                        private juce::Timer
{
public:
    explicit TrackDeckAudioProcessorEditor (TrackDeckAudioProcessor&);
    ~TrackDeckAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow (int rowNumber, bool isRowSelected,
                                              juce::Component* existingComponentToUpdate) override;

private:
    //==============================================================================
    /** One row = one LOADED slot (empty slots are hidden entirely — see
        TrackDeckAudioProcessorEditor::visibleSlots). Doubles as a timeline
        lane: it shows that file's waveform (via AudioThumbnail), and
        clicking/dragging anywhere on a lane seeks ALL loaded tracks to that
        time. Listens for change notifications from its AudioThumbnail so
        the waveform repaints itself as it decodes in the background,
        instead of only appearing the next time something else happens to
        trigger a repaint. */
    class TrackRow : public juce::Component,
                      private juce::ChangeListener
    {
    public:
        explicit TrackRow (TrackDeckAudioProcessorEditor& owner);
        ~TrackRow() override;

        /** slotIndex is the processor's internal slot (0..maxTracks-1), used
            for all mute/remove/routing/waveform lookups. displayNumber is
            the 1-based position in the visible list (1, 2, 3...) shown as
            the track number — these differ once earlier tracks are removed,
            since remaining tracks shift up in the visible list without
            changing their underlying slot. */
        void setRow (int slotIndex, int displayNumber);
        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> getContentBounds() const; // full row bounds inset by the inter-row gap (Theme::TrackList::rowGapPx)
        juce::Rectangle<int> getWaveArea() const;
        void handleSeek (const juce::MouseEvent&);
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        TrackDeckAudioProcessorEditor& editorOwner;
        int slot = -1;
        int displayNumber = 0;
        juce::AudioThumbnail* attachedThumbnail = nullptr;

        juce::Label outputLabel, nameLabel, lengthLabel;

        // Full width of the right-hand column; mute/output/delete sit below it.
        juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

        juce::ToggleButton muteButton { "Mute" };
        juce::ShapeButton removeButton { "Remove", Theme::DeleteButton::normalColour, Theme::DeleteButton::overColour, Theme::DeleteButton::downColour };

        // A single routing control: for a mono file this lists individual
        // outputs (1, 2, 3...); for a stereo file it lists adjacent pairs
        // (1+2, 3+4, ...) since stereo channels are always kept together.
        // Repopulated in setRow() to match whichever the current slot is.
        juce::Label outputTag { {}, "Output" };
        juce::ComboBox outputCombo;
    };

    //==============================================================================
    void timerCallback() override;
    void chooseAndLoadFiles();
    void refreshList();
    void updateTransportButtonStates();
    juce::AudioThumbnail* getThumbnail (int slot);

    TrackDeckAudioProcessor& processor;

    // Big, high-contrast transport controls for live performance use.
    juce::ShapeButton playButton  { "Play",  Theme::Transport::playColour,  Theme::Transport::playOverColour,  Theme::Transport::playDownColour };
    juce::ShapeButton pauseButton { "Pause", Theme::Transport::pauseColour, Theme::Transport::pauseOverColour, Theme::Transport::pauseDownColour };
    juce::ShapeButton stopButton  { "Stop",  Theme::Transport::stopColour,  Theme::Transport::stopOverColour,  Theme::Transport::stopDownColour };

    juce::Label positionLabel;
    juce::TextButton addFilesButton { "+ Add Track" };
    juce::Label helpLabel;
    juce::ListBox trackList { "Tracks", this };

    // Which processor slots are currently loaded, in slot order — rebuilt
    // every time getNumRows() is queried, so the ListBox only ever shows
    // loaded tracks and hidden empty slots take up no space in the UI.
    juce::Array<int> visibleSlots;

    juce::AudioThumbnailCache thumbnailCache { TrackDeckAudioProcessor::maxTracks + 4 };
    juce::OwnedArray<juce::AudioThumbnail> thumbnails;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackDeckAudioProcessorEditor)
};

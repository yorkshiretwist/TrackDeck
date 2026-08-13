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

    High/low-DPI handling: every size in this class and in Theme.h is a
    DPI-independent "logical pixel" value (the same units JUCE's own Font
    and Component sizing use everywhere) - never a raw physical pixel count
    read from the OS or a display. JUCE maps logical to physical pixels
    per-monitor automatically (via an AffineTransform scale applied to the
    whole component tree), so the plugin's on-screen *size* stays visually
    consistent whether it's opened on a 100% or a 200%-scaled display, and
    when its window is dragged between monitors with different scaling.
    Nothing here is bitmap/raster-based either (no image assets anywhere -
    every icon is a vector juce::Path, every label a real drawn Font), so
    there's nothing that could look pixelated or blurry at any scale factor.
    setScaleFactor() below is a small defensive hook for host-driven DPI
    change notifications; see its own comment for details. */
class TrackDeckAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::ListBoxModel,
                                        private juce::Timer
{
public:
    explicit TrackDeckAudioProcessorEditor (TrackDeckAudioProcessor&);
    ~TrackDeckAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called by the host (VST3/AAX) when it reports a new content scale
        factor - e.g. the plugin window moved to a monitor with different
        DPI, or was opened on one with a non-100% scale. JUCE's base
        implementation already does the actual work (applies an
        AffineTransform::scale() to the whole editor), so this override
        exists purely as a defensive hook: it forces a fresh resized() +
        repaint() afterwards, in case anything ever ends up depending on
        cached pixel measurements. Nothing here currently does, but this
        keeps that guaranteed rather than assumed as the UI grows. */
    void setScaleFactor (float newScale) override;

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
    void saveTracksToFile();
    void loadTracksFromFile();
    void refreshAllThumbnails(); // re-syncs every AudioThumbnail with the processor's current slots - used after a full .tracks load
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

    // Manual save/load of the current track set to/from a .tracks (YAML)
    // file on disk - entirely separate from the host's own project save.
    juce::TextButton saveTracksButton { "Save" };
    juce::TextButton loadTracksButton { "Load" };

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

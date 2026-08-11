#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Simple, unmistakable icon shapes (drawn in a 0..100 unit box, then
    // auto-scaled by ShapeButton to fill whatever size we give the button).
    juce::Path makePlayShape()
    {
        juce::Path p;
        p.addTriangle (8.0f, 4.0f, 8.0f, 96.0f, 94.0f, 50.0f);
        return p;
    }

    juce::Path makePauseShape()
    {
        juce::Path p;
        p.addRoundedRectangle (10.0f, 4.0f, 28.0f, 92.0f, 6.0f);
        p.addRoundedRectangle (62.0f, 4.0f, 28.0f, 92.0f, 6.0f);
        return p;
    }

    juce::Path makeStopShape()
    {
        juce::Path p;
        p.addRoundedRectangle (8.0f, 8.0f, 84.0f, 84.0f, 10.0f);
        return p;
    }

    // A thick "X", built by stroking two diagonal lines - used for the
    // small borderless remove/delete button.
    juce::Path makeRemoveShape()
    {
        juce::Path lines;
        lines.startNewSubPath (16.0f, 16.0f);
        lines.lineTo (84.0f, 84.0f);
        lines.startNewSubPath (84.0f, 16.0f);
        lines.lineTo (16.0f, 84.0f);

        juce::Path stroked;
        juce::PathStrokeType (16.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, lines);
        return stroked;
    }

    juce::String formatTime (double seconds)
    {
        if (seconds < 0.0)
            seconds = 0.0;

        const int totalTenths = juce::roundToInt (seconds * 10.0);
        const int mins  = (totalTenths / 10) / 60;
        const int secs  = (totalTenths / 10) % 60;
        const int tenth = totalTenths % 10;

        return juce::String::formatted ("%02d:%02d.%d", mins, secs, tenth);
    }
}

//==============================================================================
// TrackRow
//==============================================================================
TrackDeckAudioProcessorEditor::TrackRow::TrackRow (TrackDeckAudioProcessorEditor& owner)
    : editorOwner (owner)
{
    addAndMakeVisible (outputLabel);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (lengthLabel);
    addAndMakeVisible (volumeSlider);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (removeButton);
    addAndMakeVisible (outputTag);
    addAndMakeVisible (outputCombo);

    outputLabel.setJustificationType (juce::Justification::centred);
    outputLabel.setColour (juce::Label::textColourId, Theme::TrackRow::numberTextColour);
    outputLabel.setFont (juce::Font (Theme::TrackRow::numberFontSizePx, juce::Font::bold));

    nameLabel.setJustificationType (juce::Justification::topLeft);
    nameLabel.setColour (juce::Label::textColourId, Theme::TrackRow::nameTextColour);
    nameLabel.setInterceptsMouseClicks (false, false); // let clicks fall through to the row for seeking

    lengthLabel.setJustificationType (juce::Justification::bottomLeft);
    lengthLabel.setColour (juce::Label::textColourId, Theme::TrackRow::lengthTextColour);
    lengthLabel.setFont (juce::Font (Theme::TrackRow::lengthFontSizePx));
    lengthLabel.setInterceptsMouseClicks (false, false);

    volumeSlider.setRange (0.0, 1.0, 0.0); // linear gain: 0.0 silent, 1.0 unity/full
    volumeSlider.setValue (1.0, juce::dontSendNotification);
    volumeSlider.setColour (juce::Slider::backgroundColourId, Theme::VolumeSlider::unfilledTrackColour);
    volumeSlider.setColour (juce::Slider::trackColourId, Theme::VolumeSlider::filledTrackColour);
    volumeSlider.setColour (juce::Slider::thumbColourId, Theme::VolumeSlider::thumbColour);

    volumeSlider.onValueChange = [this]
    {
        if (slot >= 0)
            editorOwner.processor.setSlotVolume (slot, (float) volumeSlider.getValue());
    };

    muteButton.setColour (juce::ToggleButton::textColourId, Theme::MuteButton::textColour);
    muteButton.setColour (juce::ToggleButton::tickColourId, Theme::MuteButton::tickColour);
    muteButton.setColour (juce::ToggleButton::tickDisabledColourId, Theme::MuteButton::borderColour); // box outline when untoggled

    muteButton.onClick = [this]
    {
        if (slot >= 0)
        {
            editorOwner.processor.setSlotMuted (slot, muteButton.getToggleState());
            repaint(); // the waveform colour (grey when muted) is drawn in our own paint()
        }
    };

    removeButton.setShape (makeRemoveShape(), true, true, false);
    // Deliberately no setOutline() call - a flat, borderless icon button.

    removeButton.onClick = [this]
    {
        if (slot >= 0)
        {
            if (auto* thumb = editorOwner.getThumbnail (slot))
                thumb->clear();

            editorOwner.processor.removeSlot (slot);
            editorOwner.refreshList();
        }
    };

    // Output-routing selector: which physical output(s) this file is sent
    // to. Repopulated per-row in setRow() since the item list itself
    // differs between mono (individual outputs) and stereo (adjacent
    // pairs) files.
    outputTag.setJustificationType (juce::Justification::centred);
    outputTag.setColour (juce::Label::textColourId, Theme::ChannelDropdown::tagLabelColour);
    outputTag.setFont (juce::Font (Theme::ChannelDropdown::tagFontSizePx));

    outputCombo.setColour (juce::ComboBox::backgroundColourId, Theme::ChannelDropdown::backgroundColour);
    outputCombo.setColour (juce::ComboBox::outlineColourId, Theme::ChannelDropdown::borderColour);
    outputCombo.setColour (juce::ComboBox::textColourId, Theme::ChannelDropdown::textColour);

    outputCombo.onChange = [this]
    {
        if (slot < 0)
            return;

        const int selectedId = outputCombo.getSelectedId();

        if (editorOwner.processor.getSlotNumChannels (slot) > 1)
            editorOwner.processor.setSlotOutputPair (slot, selectedId - 1);
        else
            editorOwner.processor.setSlotOutputChannel (slot, 0, selectedId - 1);
    };
}

TrackDeckAudioProcessorEditor::TrackRow::~TrackRow()
{
    if (attachedThumbnail != nullptr)
        attachedThumbnail->removeChangeListener (this);
}

void TrackDeckAudioProcessorEditor::TrackRow::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The AudioThumbnail broadcasts this as it decodes more of the file in
    // the background (and again when cleared), so repaint to show progress.
    repaint();
}

void TrackDeckAudioProcessorEditor::TrackRow::setRow (int slotIndex, int newDisplayNumber)
{
    slot = slotIndex;
    displayNumber = newDisplayNumber;

    // ListBox reuses TrackRow components across different slot indices as it
    // scrolls, so re-point our change listener at whichever thumbnail this
    // row now represents.
    auto* newThumbnail = juce::isPositiveAndBelow (slot, TrackDeckAudioProcessor::maxTracks)
                              ? editorOwner.getThumbnail (slot)
                              : nullptr;

    if (newThumbnail != attachedThumbnail)
    {
        if (attachedThumbnail != nullptr)
            attachedThumbnail->removeChangeListener (this);

        attachedThumbnail = newThumbnail;

        if (attachedThumbnail != nullptr)
            attachedThumbnail->addChangeListener (this);
    }

    if (! juce::isPositiveAndBelow (slot, TrackDeckAudioProcessor::maxTracks) || ! editorOwner.processor.isSlotLoaded (slot))
    {
        // Rows are only ever built for loaded slots now, but guard anyway
        // in case this gets called with a stale/invalid index mid-refresh.
        nameLabel.setText ("(empty)", juce::dontSendNotification);
        lengthLabel.setText ({}, juce::dontSendNotification);
        volumeSlider.setEnabled (false);
        volumeSlider.setVisible (false);
        muteButton.setToggleState (false, juce::dontSendNotification);
        muteButton.setEnabled (false);
        removeButton.setEnabled (false);

        outputTag.setVisible (false);
        outputCombo.setVisible (false);

        repaint();
        return;
    }

    outputLabel.setText (juce::String (displayNumber), juce::dontSendNotification);

    nameLabel.setText (editorOwner.processor.getSlotFileName (slot), juce::dontSendNotification);

    const double secs = editorOwner.processor.getSlotLengthSeconds (slot);
    lengthLabel.setText (formatTime (secs), juce::dontSendNotification);

    volumeSlider.setEnabled (true);
    volumeSlider.setVisible (true);
    volumeSlider.setValue (editorOwner.processor.getSlotVolume (slot), juce::dontSendNotification);

    muteButton.setToggleState (editorOwner.processor.isSlotMuted (slot), juce::dontSendNotification);
    muteButton.setEnabled (true);
    removeButton.setEnabled (true);

    const int numCh = editorOwner.processor.getSlotNumChannels (slot);
    const bool isStereo = numCh > 1;

    outputTag.setText ("Output", juce::dontSendNotification);
    outputTag.setVisible (true);
    outputCombo.setVisible (true);
    outputCombo.setEnabled (true);

    outputCombo.clear (juce::dontSendNotification);

    if (isStereo)
    {
        // Adjacent pairs only (1+2, 3+4, ...) - stereo channels are always
        // kept together, so this is the only choice offered.
        for (int pair = 0; pair * 2 + 1 < TrackDeckAudioProcessor::maxOutputChannels; ++pair)
            outputCombo.addItem (juce::String (pair * 2 + 1) + "+" + juce::String (pair * 2 + 2), pair + 1);

        const int leftOutput = editorOwner.processor.getSlotOutputChannel (slot, 0);
        outputCombo.setSelectedId (leftOutput / 2 + 1, juce::dontSendNotification);
    }
    else
    {
        for (int i = 0; i < TrackDeckAudioProcessor::maxOutputChannels; ++i)
            outputCombo.addItem (juce::String (i + 1), i + 1);

        outputCombo.setSelectedId (editorOwner.processor.getSlotOutputChannel (slot, 0) + 1, juce::dontSendNotification);
    }

    repaint();
}

juce::Rectangle<int> TrackDeckAudioProcessorEditor::TrackRow::getContentBounds() const
{
    // Insetting vertically by half the configured gap on each row leaves a
    // visible strip of TrackList::backgroundColour between adjacent rows.
    return getLocalBounds().reduced (0, Theme::TrackList::rowGapPx / 2);
}

juce::Rectangle<int> TrackDeckAudioProcessorEditor::TrackRow::getWaveArea() const
{
    return getContentBounds().withTrimmedLeft (Theme::TrackRow::leftMarginPx)
                              .withTrimmedRight (Theme::TrackRow::rightMarginPx)
                              .reduced (0, 2);
}

void TrackDeckAudioProcessorEditor::TrackRow::paint (juce::Graphics& g)
{
    auto contentBounds = getContentBounds();

    g.setColour (Theme::TrackRow::backgroundColour);
    g.fillRect (contentBounds);

    if (! juce::isPositiveAndBelow (slot, TrackDeckAudioProcessor::maxTracks) || ! editorOwner.processor.isSlotLoaded (slot))
        return;

    auto waveArea = getWaveArea();

    const double maxLen  = editorOwner.processor.getLongestLoadedLengthSeconds();
    const double thisLen = editorOwner.processor.getSlotLengthSeconds (slot);

    if (maxLen > 0.0 && thisLen > 0.0)
    {
        if (auto* thumb = editorOwner.getThumbnail (slot))
        {
            const int fullWidth = waveArea.getWidth();
            const int thisWidth = juce::jlimit (1, fullWidth, (int) (fullWidth * (thisLen / maxLen)));
            auto activeArea = waveArea.withWidth (thisWidth);

            g.setColour (editorOwner.processor.isSlotMuted (slot) ? Theme::Waveform::mutedPeaksColour
                                                                     : Theme::Waveform::peaksColour);
            thumb->drawChannels (g, activeArea, 0.0, thisLen, 1.0f);
        }

        // Marker at the waveform's start (time zero), on top of the peaks.
        g.setColour (Theme::Waveform::leftBorderColour);
        g.drawVerticalLine (waveArea.getX(), (float) contentBounds.getY(), (float) contentBounds.getBottom());

        // Shared playhead: every row draws its segment at the same x, so
        // stacked rows line up into one continuous vertical cursor.
        const double pos = editorOwner.processor.getPlaybackPositionSeconds();
        const int playheadX = waveArea.getX() + (int) (waveArea.getWidth() * juce::jlimit (0.0, 1.0, pos / maxLen));

        g.setColour (Theme::Waveform::playheadColour);
        g.drawVerticalLine (playheadX, (float) contentBounds.getY(), (float) contentBounds.getBottom());
    }

    // Backdrop behind the file name only (not the length text below it),
    // sized tightly to the actual text rather than the whole label area, so
    // it stays readable over the waveform while covering as little of it as
    // possible.
    if (nameLabel.getText().isNotEmpty())
    {
        const auto nameBounds = nameLabel.getBounds();
        const auto font = nameLabel.getFont();
        const juce::String text = nameLabel.getText();

        // Measured via GlyphArrangement rather than Font::getStringWidthFloat()
        // (removed in JUCE 8's font rewrite) so this compiles against both
        // JUCE 7.x and 8.x.
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        const float measuredWidth = glyphs.getBoundingBox (0, -1, true).getWidth();

        const int textW = juce::jmin (nameBounds.getWidth(), juce::roundToInt (measuredWidth) + 10);
        const int textH = juce::jmin (nameBounds.getHeight(), juce::roundToInt (font.getHeight()) + 6);

        g.setColour (Theme::TrackRow::nameBackdropColour);
        g.fillRoundedRectangle ((float) nameBounds.getX() - 2.0f, (float) nameBounds.getY() - 1.0f,
                                 (float) textW, (float) textH, 3.0f);
    }
}

void TrackDeckAudioProcessorEditor::TrackRow::resized()
{
    auto area = getContentBounds().reduced (2);

    outputLabel.setBounds (area.removeFromLeft (Theme::TrackRow::leftMarginPx));

    auto rightArea = area.removeFromRight (Theme::TrackRow::rightMarginPx);

    // Volume slider spans the full width of the right-hand column, with
    // everything else (mute/output/delete) stacked below it.
    volumeSlider.setBounds (rightArea.removeFromTop (Theme::VolumeSlider::heightPx)
                                      .reduced (Theme::VolumeSlider::horizontalInsetPx, 0));
    rightArea.removeFromTop (Theme::VolumeSlider::gapBelowPx);

    // Top half of what's left: mute + a small, borderless remove button.
    auto topRight = rightArea.removeFromTop (rightArea.getHeight() / 2);
    removeButton.setBounds (topRight.removeFromRight (Theme::DeleteButton::sizePx).reduced (4));
    muteButton.setBounds (topRight.reduced (2));

    // Bottom half: the single output routing selector, with its "Output" tag.
    auto bottomRight = rightArea.reduced (2);
    outputTag.setBounds (bottomRight.removeFromLeft (44));
    outputCombo.setBounds (bottomRight);

    // Name/length are drawn as an overlay on top of the waveform area.
    auto textArea = getWaveArea().reduced (6, 2);
    nameLabel.setBounds (textArea.removeFromTop (textArea.getHeight() / 2));
    lengthLabel.setBounds (textArea);
}

void TrackDeckAudioProcessorEditor::TrackRow::mouseDown (const juce::MouseEvent& e) { handleSeek (e); }
void TrackDeckAudioProcessorEditor::TrackRow::mouseDrag (const juce::MouseEvent& e) { handleSeek (e); }

void TrackDeckAudioProcessorEditor::TrackRow::handleSeek (const juce::MouseEvent& e)
{
    const double maxLen = editorOwner.processor.getLongestLoadedLengthSeconds();

    if (maxLen <= 0.0)
        return;

    auto waveArea = getWaveArea();

    if (waveArea.getWidth() <= 0 || e.x < waveArea.getX() || e.x > waveArea.getRight())
        return;

    const double fraction = juce::jlimit (0.0, 1.0, (e.x - waveArea.getX()) / (double) waveArea.getWidth());
    editorOwner.processor.seekTo (fraction * maxLen);
    editorOwner.refreshList();
}

//==============================================================================
// Editor
//==============================================================================
TrackDeckAudioProcessorEditor::TrackDeckAudioProcessorEditor (TrackDeckAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    for (int i = 0; i < TrackDeckAudioProcessor::maxTracks; ++i)
        thumbnails.add (new juce::AudioThumbnail (512, processor.formatManager, thumbnailCache));

    // Re-populate thumbnails for any slots already loaded (e.g. restored
    // from a saved session before this editor window was opened).
    for (int i = 0; i < TrackDeckAudioProcessor::maxTracks; ++i)
        if (processor.isSlotLoaded (i))
            thumbnails[i]->setSource (new juce::FileInputSource (processor.getSlotFile (i)));

    addAndMakeVisible (playButton);
    addAndMakeVisible (pauseButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (addFilesButton);
    addAndMakeVisible (helpLabel);
    addAndMakeVisible (trackList);

    playButton.setShape (makePlayShape(), true, true, false);
    pauseButton.setShape (makePauseShape(), true, true, false);
    stopButton.setShape (makeStopShape(), true, true, false);

    playButton.onClick  = [this] { processor.startPlayback(); updateTransportButtonStates(); };
    pauseButton.onClick = [this] { processor.pausePlayback(); updateTransportButtonStates(); };
    stopButton.onClick  = [this] { processor.stopPlayback();  updateTransportButtonStates(); refreshList(); };

    positionLabel.setJustificationType (juce::Justification::centredRight);
    positionLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), Theme::Transport::timeFontSizePx, juce::Font::bold));
    positionLabel.setColour (juce::Label::textColourId, Theme::Transport::timeTextColour);

    addFilesButton.setColour (juce::TextButton::buttonColourId, Theme::AddTrackButton::backgroundColour);
    addFilesButton.setColour (juce::TextButton::textColourOffId, Theme::AddTrackButton::textColour);
    addFilesButton.setColour (juce::TextButton::textColourOnId, Theme::AddTrackButton::textColour);
    addFilesButton.onClick = [this] { chooseAndLoadFiles(); };

    helpLabel.setText ("Tap a waveform to jump every track to that time. Output selects each file's physical output(s).",
                        juce::dontSendNotification);
    helpLabel.setFont (juce::Font (Theme::HelpText::fontSizePx));
    helpLabel.setColour (juce::Label::textColourId, Theme::HelpText::textColour);
    helpLabel.setJustificationType (juce::Justification::centredLeft);

    trackList.setRowHeight (Theme::TrackList::rowHeightPx);
    trackList.setColour (juce::ListBox::backgroundColourId, Theme::TrackList::backgroundColour);

    updateTransportButtonStates();
    refreshList(); // build the initial visible-track list (only loaded slots, if any were restored)

    setResizable (true, true);
    setResizeLimits (560, 400, 3000, 2000);
    setSize (760, 600);
    startTimerHz (30); // smooth playhead motion during playback/scrubbing
}

TrackDeckAudioProcessorEditor::~TrackDeckAudioProcessorEditor() = default;

void TrackDeckAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::Window::backgroundColour);

    g.setColour (Theme::Window::titleTextColour);
    g.setFont (Theme::Window::titleFontSizePx);
    g.drawFittedText ("TrackDeck",
                       getLocalBounds().removeFromTop (30).reduced (10, 0),
                       juce::Justification::centredLeft, 1);
}

void TrackDeckAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (30); // title

    // Big transport row - the primary live-performance controls.
    auto transportRow = area.removeFromTop (Theme::Transport::rowHeightPx);
    const int buttonSize = Theme::Transport::buttonSizePx;

    playButton.setBounds  (transportRow.removeFromLeft (buttonSize));
    transportRow.removeFromLeft (10);
    pauseButton.setBounds (transportRow.removeFromLeft (buttonSize));
    transportRow.removeFromLeft (10);
    stopButton.setBounds  (transportRow.removeFromLeft (buttonSize));
    transportRow.removeFromLeft (16);

    positionLabel.setBounds (transportRow);

    area.removeFromTop (10);

    // "Add Files" + help text are pinned to the bottom, below the
    // scrollable track list, so they stay reachable regardless of how many
    // tracks are loaded or how the window is resized.
    auto bottomRow = area.removeFromBottom (34);
    addFilesButton.setBounds (bottomRow.removeFromLeft (140));
    bottomRow.removeFromLeft (10);
    helpLabel.setBounds (bottomRow);

    area.removeFromBottom (8);
    trackList.setBounds (area);
}

int TrackDeckAudioProcessorEditor::getNumRows()
{
    // Rebuilt on every call (cheap - maxTracks is small) so the ListBox
    // always reflects exactly which slots are currently loaded, with no
    // rows shown for empty ones.
    visibleSlots.clearQuick();

    for (int i = 0; i < TrackDeckAudioProcessor::maxTracks; ++i)
        if (processor.isSlotLoaded (i))
            visibleSlots.add (i);

    return visibleSlots.size();
}

void TrackDeckAudioProcessorEditor::paintListBoxItem (int, juce::Graphics&, int, int, bool)
{
    // All drawing is done by the TrackRow child component itself.
}

juce::Component* TrackDeckAudioProcessorEditor::refreshComponentForRow (int rowNumber, bool,
                                                                           juce::Component* existingComponentToUpdate)
{
    auto* trackRow = dynamic_cast<TrackRow*> (existingComponentToUpdate);

    if (trackRow == nullptr)
    {
        delete existingComponentToUpdate;
        trackRow = new TrackRow (*this);
    }

    const int slot = juce::isPositiveAndBelow (rowNumber, visibleSlots.size()) ? visibleSlots.getUnchecked (rowNumber) : -1;
    trackRow->setRow (slot, rowNumber + 1); // slot for data lookups, rowNumber+1 as the displayed track number

    return trackRow;
}

void TrackDeckAudioProcessorEditor::timerCallback()
{
    positionLabel.setText (formatTime (processor.getPlaybackPositionSeconds()), juce::dontSendNotification);

    if (processor.isPlaying())
        trackList.repaint(); // keep the shared playhead moving across all lanes
}

void TrackDeckAudioProcessorEditor::updateTransportButtonStates()
{
    const bool nowPlaying = processor.isPlaying();

    // No border on these buttons - indicate the active state with a
    // brighter fill colour instead.
    playButton.setColours (nowPlaying ? Theme::Transport::playActiveColour : Theme::Transport::playColour,
                            Theme::Transport::playOverColour,
                            Theme::Transport::playDownColour);
}

juce::AudioThumbnail* TrackDeckAudioProcessorEditor::getThumbnail (int slot)
{
    return juce::isPositiveAndBelow (slot, thumbnails.size()) ? thumbnails[slot] : nullptr;
}

void TrackDeckAudioProcessorEditor::chooseAndLoadFiles()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Select audio files...",
                                                         juce::File(),
                                                         "*.wav;*.mp3;*.flac");

    const auto flags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectMultipleItems;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto files = fc.getResults();
        int slot = 0;
        bool hitTrackLimit = false;

        for (auto& f : files)
        {
            while (slot < TrackDeckAudioProcessor::maxTracks && processor.isSlotLoaded (slot))
                ++slot;

            if (slot >= TrackDeckAudioProcessor::maxTracks)
            {
                hitTrackLimit = true;
                break; // no free slots left - stop loading further files
            }

            if (processor.loadFileIntoSlot (slot, f))
                if (auto* thumb = getThumbnail (slot))
                    thumb->setSource (new juce::FileInputSource (f));

            ++slot;
        }

        refreshList();

        if (hitTrackLimit)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                      "Track limit reached",
                                                      "Sorry, only 16 tracks can be loaded.");
        }
    });
}

void TrackDeckAudioProcessorEditor::refreshList()
{
    trackList.updateContent(); // calls getNumRows(), which rebuilds visibleSlots
    trackList.repaint();

    addFilesButton.setEnabled (visibleSlots.size() < TrackDeckAudioProcessor::maxTracks);
}

#pragma once

#include <JuceHeader.h>
#include "FilePlayer.h"

/**
    A plugin with no audio input and `maxOutputChannels` mono output buses
    (physical outputs). Up to `maxTracks` files can be loaded; each file's
    channel(s) are independently routed to any of those mono output buses.

    Default routing: a stereo file gets the next free *adjacent* pair of
    outputs (1+2, then 3+4, etc.), a mono file gets the next free single
    output - so loading files in order fills outputs 1,2,3... sensibly,
    matching what you'd expect from a simple live-playback rig. The user can
    override any file's routing from the UI at any time; stereo files are
    always kept on an adjacent pair (never split across non-adjacent
    outputs), since that's what the single "Output" pair-selector dropdown
    offers.

    Pressing "play" starts every loaded file from the same shared transport
    position in the same processBlock() call, so they stay sample-accurately
    in sync regardless of how their channels are routed.

    Settings persistence: loaded files, mute state, volume, and output routing
    survive two ways. (1) The normal host mechanism - getStateInformation()/
    setStateInformation() - saves/restores with whatever DAW project you're
    working in, same as any other plugin. (2) Independently of that, every
    change is also written to a small settings file on disk (see
    getPersistedSettingsFile()), and that file is auto-loaded the moment a
    new plugin instance is constructed - so even inserting a *fresh* instance
    with no saved project, in a DAW that was fully closed and reopened, comes
    back with whatever was loaded last. If the host then applies its own
    saved project state via setStateInformation(), that (project-specific)
    state takes over, as you'd expect.
*/
class TrackDeckAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxTracks = 16;
    static constexpr int maxOutputChannels = 32;

    TrackDeckAudioProcessor();
    ~TrackDeckAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Control API used by the editor UI.

    /** Loads a file into a given slot (0..maxTracks-1) and assigns it sensible
        default output routing (see class comment). Returns false on decode failure. */
    bool loadFileIntoSlot (int slot, const juce::File& file);

    /** Removes whatever is loaded in a slot. */
    void removeSlot (int slot);

    void setSlotMuted (int slot, bool muted);
    bool isSlotMuted (int slot) const;

    /** Linear volume, 0.0 (silent) to 1.0 (unity/full). Defaults to 1.0 for
        a freshly-loaded file. Saved/restored the same way as everything
        else - see the class comment. */
    void setSlotVolume (int slot, float volume);
    float getSlotVolume (int slot) const;

    bool isSlotLoaded (int slot) const;
    juce::String getSlotFileName (int slot) const;
    juce::File getSlotFile (int slot) const;
    double getSlotLengthSeconds (int slot) const;

    /** Number of channels (1 = mono, 2 = stereo) the file in this slot has. 0 if empty. */
    int getSlotNumChannels (int slot) const;

    /** Which output bus (0-based, 0..maxOutputChannels-1) a given file channel
        (0 = mono/left, 1 = right) is routed to, or -1 if unassigned/unloaded. */
    int getSlotOutputChannel (int slot, int fileChannel) const;

    /** Explicitly (re)routes one channel of a MONO file to an output bus.
        For stereo files, use setSlotOutputPair() instead - the UI only
        offers stereo files an adjacent-pair choice (1+2, 3+4, ...), never
        independent L/R routing, since split stereo routing is rarely
        wanted and it keeps the control simple. */
    void setSlotOutputChannel (int slot, int fileChannel, int outputIndex);

    /** Routes a STEREO file's left/right channels to an adjacent output
        pair: pairIndex 0 = outputs 1+2, pairIndex 1 = outputs 3+4, etc.
        (0-based internally, i.e. left = pairIndex*2, right = pairIndex*2+1).
        No-op if the slot isn't loaded or isn't stereo. */
    void setSlotOutputPair (int slot, int pairIndex);

    /** Longest currently-loaded file, in seconds. 0 if nothing is loaded.
        Used to lay out the shared timeline. */
    double getLongestLoadedLengthSeconds() const;

    /** Starts every loaded slot together from the current shared position. */
    void startPlayback();
    /** Stops playback but leaves the shared position where it was, so
        pressing Play again resumes from the same point. */
    void pausePlayback();
    /** Stops playback and rewinds the shared position to zero. */
    void stopPlayback();
    void togglePlayback();
    bool isPlaying() const { return playing.load(); }

    /** Moves the shared transport position immediately. If playback is
        currently running, every loaded slot is repositioned right away so
        the audio jumps to the new time with no audible gap; if stopped,
        the new position takes effect the next time Play is pressed. Used
        for click-to-seek on the timeline. */
    void seekTo (double newPositionSeconds);

    double getPlaybackPositionSeconds() const
    {
        return currentSampleRate > 0.0 ? (double) playbackPositionSamples.load() / currentSampleRate : 0.0;
    }

    juce::AudioFormatManager formatManager;

private:
    static juce::String busNameForOutput (int index) { return "Output " + juce::String (index + 1); }
    static BusesProperties buildBusesProperties();

    /** Assigns the next free consecutive output bus block to every channel of
        the file just loaded into `slot`, without disturbing any other slot's
        existing (possibly user-customised) routing. */
    void assignDefaultOutputs (int slot);

    /** Builds/applies the full "loaded files + mute + routing" state as a
        ValueTree. Shared by both the host state mechanism
        (getStateInformation/setStateInformation) and the on-disk persisted
        settings (savePersistedSettings/loadPersistedSettings) below, so
        there's exactly one place that knows the on-disk/in-session format. */
    juce::ValueTree buildStateTree() const;
    void applyStateTree (const juce::ValueTree& state);

    /** Where the on-disk "remember settings between DAW sessions" file lives
        (a per-user location, independent of any DAW project file). */
    static juce::File getPersistedSettingsFile();

    /** Writes the current state to getPersistedSettingsFile(). Called after
        every change (load/remove/mute/routing) so the on-disk copy is always
        current. Cheap and only ever called from the message thread. */
    void savePersistedSettings() const;

    /** Tells the host that the plugin's non-parameter state changed (a file
        was loaded/removed/muted, or routing changed), via
        AudioProcessor::updateHostDisplay(). This is what makes compliant
        hosts show their "project needs saving" indicator - none of our
        state is exposed as an automatable APVTS parameter, so without this
        call the host has no way to know anything changed. Called alongside
        savePersistedSettings() after every mutating action. */
    void notifyHostOfStateChange();

    /** Loads getPersistedSettingsFile() if it exists. Called once from the
        constructor so a freshly-created instance starts with whatever was
        loaded last, even with no DAW project involved at all. */
    void loadPersistedSettings();

    juce::OwnedArray<FilePlayer> players;

    std::atomic<bool> playing { false };
    std::atomic<juce::int64> playbackPositionSamples { 0 };

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackDeckAudioProcessor)
};

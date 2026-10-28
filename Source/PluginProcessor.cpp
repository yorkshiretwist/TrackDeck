#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    //==============================================================================
    // Small helpers for the hand-rolled YAML subset used by .tracks files.
    // These deliberately only handle exactly what stateTreeToYaml() writes -
    // this isn't a general-purpose YAML reader/writer.

    juce::String yamlQuoteString (const juce::String& s)
    {
        return "\"" + s.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\"";
    }

    juce::String yamlUnquoteString (const juce::String& raw)
    {
        juce::String s = raw.trim();

        if (s.length() >= 2 && s.startsWithChar ('"') && s.endsWithChar ('"'))
            s = s.substring (1, s.length() - 1).replace ("\\\"", "\"").replace ("\\\\", "\\");

        return s;
    }

    // "[0, 1]" -> "0,1" (matches the CSV format buildStateTree()'s "outputs"
    // property already uses elsewhere).
    juce::String yamlIntListToCsv (const juce::String& raw)
    {
        juce::String s = raw.trim();

        if (s.startsWithChar ('['))
            s = s.substring (1);

        if (s.endsWithChar (']'))
            s = s.dropLastCharacters (1);

        juce::StringArray tokens;
        tokens.addTokens (s, ",", "");

        for (auto& t : tokens)
            t = t.trim();

        tokens.removeEmptyStrings();

        return tokens.joinIntoString (",");
    }

    // Keys used for the MIDI CC block in .tracks files and for the matching
    // properties on the state tree's root, indexed by MidiCommand.
    const char* const midiCommandKeys[] = { "play", "pause", "stop", "toggle" };

    // Name of the state tree property holding the shared MIDI channel. In
    // .tracks files the same setting is the "channel:" line of the midi block.
    const char* const midiChannelStateKey = "midiChannel";

    // The matching key for a command's CC value, e.g. "playValue".
    juce::String midiValueKey (int commandIndex)
    {
        return juce::String (midiCommandKeys[commandIndex]) + "Value";
    }

    // "any" is how the any-channel setting is written; anything that isn't a
    // channel from 1 to 16 is read as any channel.
    int yamlParseMidiChannel (const juce::String& raw)
    {
        const juce::String s = raw.trim();

        if (s.isEmpty() || ! s.containsOnly ("0123456789"))
            return TrackDeckAudioProcessor::midiChannelAny;

        const int channel = s.getIntValue();
        return juce::isPositiveAndBelow (channel, 17) ? channel : TrackDeckAudioProcessor::midiChannelAny;
    }

    // "none" is how an unassigned CC is written; anything that isn't a
    // number from 0 to 127 is read as unassigned.
    int yamlParseMidiCc (const juce::String& raw)
    {
        const juce::String s = raw.trim();

        if (s.isEmpty() || ! s.containsOnly ("0123456789"))
            return TrackDeckAudioProcessor::midiCcUnassigned;

        const int cc = s.getIntValue();
        return juce::isPositiveAndBelow (cc, 128) ? cc : TrackDeckAudioProcessor::midiCcUnassigned;
    }
}

//==============================================================================
const juce::String TrackDeckAudioProcessor::tracksFileExtension = "tracks";

juce::AudioProcessor::BusesProperties TrackDeckAudioProcessor::buildBusesProperties()
{
    BusesProperties props;

    for (int i = 0; i < maxOutputChannels; ++i)
        props = props.withOutput (busNameForOutput (i), juce::AudioChannelSet::mono(), i < defaultNumOutputs);

    return props;
}

void TrackDeckAudioProcessor::setNumOutputs (int newNumOutputs)
{
    // Even, so stereo pairs always line up.
    newNumOutputs = juce::jlimit (minNumOutputs, maxOutputChannels, newNumOutputs & ~1);

    if (newNumOutputs == numOutputs.load())
        return;

    numOutputs.store (newNumOutputs);

    // Not every host lets a plugin change its buses, in which case the extra
    // buses just stay as they are; they're silent either way.
    for (int i = 0; i < getBusCount (false); ++i)
        if (auto* bus = getBus (false, i))
            bus->enable (i < newNumOutputs);

    // Anything routed to an output that no longer exists gets a fresh default.
    for (int slot = 0; slot < players.size(); ++slot)
    {
        if (! players[slot]->isLoaded())
            continue;

        for (int c = 0; c < players[slot]->getNumChannels(); ++c)
        {
            if (players[slot]->getOutputForChannel (c) >= newNumOutputs)
            {
                assignDefaultOutputs (slot);
                break;
            }
        }
    }

    notifyHostOfStateChange();
}

TrackDeckAudioProcessor::TrackDeckAudioProcessor()
    : AudioProcessor (buildBusesProperties())
{
    // Registers WAV, AIFF, FLAC and (with JUCE_USE_MP3AUDIOFORMAT=1) MP3 decoding.
    formatManager.registerBasicFormats();

    for (int i = 0; i < maxTracks; ++i)
        players.add (new FilePlayer (formatManager));

    resetMidiCcsToDefaults();

    // Deliberately no auto-restore here - every new instance (standalone or
    // freshly inserted in a host) starts with a blank set of tracks. See
    // the class comment for how state is handled instead.
}

TrackDeckAudioProcessor::~TrackDeckAudioProcessor()
{
    cancelPendingUpdate();
}

//==============================================================================
juce::String TrackDeckAudioProcessor::getMidiCommandName (MidiCommand command)
{
    switch (command)
    {
        case MidiCommand::play:   return "Play";
        case MidiCommand::pause:  return "Pause";
        case MidiCommand::stop:   return "Stop";
        case MidiCommand::toggle: return "Play/Pause";
    }

    return {};
}

int TrackDeckAudioProcessor::getMidiCc (MidiCommand command) const
{
    return midiCcs[(size_t) command].load();
}

int TrackDeckAudioProcessor::getMidiValue (MidiCommand command) const
{
    return midiValues[(size_t) command].load();
}

int TrackDeckAudioProcessor::getMidiChannel() const
{
    return midiChannel.load();
}

void TrackDeckAudioProcessor::setMidiChannel (int channel)
{
    midiChannel.store (juce::isPositiveAndBelow (channel, 17) ? channel : midiChannelAny);
}

void TrackDeckAudioProcessor::setMidiCc (MidiCommand command, int cc)
{
    if (! juce::isPositiveAndBelow (cc, 128))
        cc = midiCcUnassigned;

    midiCcs[(size_t) command].store (cc);
    unassignDuplicatesOf (command);
}

void TrackDeckAudioProcessor::setMidiValue (MidiCommand command, int value)
{
    midiValues[(size_t) command].store (juce::jlimit (0, 127, value));
    unassignDuplicatesOf (command);
}

void TrackDeckAudioProcessor::unassignDuplicatesOf (MidiCommand command)
{
    const int cc = midiCcs[(size_t) command].load();

    if (cc == midiCcUnassigned)
        return;

    const int value = midiValues[(size_t) command].load();

    for (size_t i = 0; i < midiCcs.size(); ++i)
        if (i != (size_t) command && midiCcs[i].load() == cc && midiValues[i].load() == value)
            midiCcs[i].store (midiCcUnassigned);
}

void TrackDeckAudioProcessor::resetMidiCcsToDefaults()
{
    for (int i = 0; i < numMidiCommands; ++i)
    {
        midiCcs[(size_t) i].store (getDefaultMidiCc ((MidiCommand) i));
        midiValues[(size_t) i].store (defaultMidiValue);
    }

    midiChannel.store (midiChannelAny);
}

void TrackDeckAudioProcessor::beginMidiLearn (MidiCommand command)
{
    midiLearnTarget.store ((int) command);
}

void TrackDeckAudioProcessor::cancelMidiLearn()
{
    midiLearnTarget.store (-1);
}

void TrackDeckAudioProcessor::handleAsyncUpdate()
{
    notifyHostOfStateChange();
}

void TrackDeckAudioProcessor::handleMidiCc (int channel, int cc, int value)
{
    if (! juce::isPositiveAndBelow (cc, 128))
        return;

    const int learning = midiLearnTarget.load();

    if (learning >= 0)
    {
        // The message that completes learning is consumed, not also acted on.
        // It sets the value as well as the number, so whatever the control
        // sends when pressed is exactly what will trigger the command.
        midiCcs[(size_t) learning].store (cc);
        midiValues[(size_t) learning].store (juce::jlimit (0, 127, value));
        unassignDuplicatesOf ((MidiCommand) learning);
        midiLearnTarget.store (-1);
        triggerAsyncUpdate();
        return;
    }

    // Exact match on number and value, on the chosen channel unless that's
    // any - nothing else triggers.
    const int wantedChannel = midiChannel.load();

    if (wantedChannel != midiChannelAny && wantedChannel != channel)
        return;

    for (int i = 0; i < numMidiCommands; ++i)
        if (midiCcs[(size_t) i].load() == cc && midiValues[(size_t) i].load() == value)
            triggerMidiCommand ((MidiCommand) i);
}

void TrackDeckAudioProcessor::triggerMidiCommand (MidiCommand command)
{
    // A command asking for the state the transport is already in is ignored.
    // "Paused" and "stopped" are both not-playing; they differ only in
    // whether the position has been rewound to zero.
    const bool isPlayingNow = playing.load();
    const bool isAtStart = playbackPositionSamples.load() == 0;

    switch (command)
    {
        case MidiCommand::play:   if (! isPlayingNow)                startPlayback();  break;
        case MidiCommand::pause:  if (isPlayingNow)                  pausePlayback();  break;
        case MidiCommand::stop:   if (isPlayingNow || ! isAtStart)   stopPlayback();   break;
        case MidiCommand::toggle: togglePlayback(); break;
    }
}

//==============================================================================
void TrackDeckAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    for (auto* p : players)
        p->prepare (sampleRate, samplesPerBlock);

    recomputeLongestLoadedLength(); // the samples-per-second conversion depends on currentSampleRate, just set above
}

void TrackDeckAudioProcessor::releaseResources()
{
    for (auto* p : players)
        p->releaseResources();
}

bool TrackDeckAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Every output bus must be either mono or fully disabled. No input buses.
    if (layouts.inputBuses.size() > 0)
        return false;

    for (auto& set : layouts.outputBuses)
        if (! set.isDisabled() && set != juce::AudioChannelSet::mono())
            return false;

    return true;
}

void TrackDeckAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // Transport commands from MIDI take effect from the start of this block
    // (a block is a few ms at most, so this is well within what's audible).
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (message.isController())
            handleMidiCc (message.getChannel(), message.getControllerNumber(), message.getControllerValue());
    }

    const bool nowPlaying = playing.load();

    if (nowPlaying)
    {
        for (auto* player : players)
        {
            if (! player->isLoaded())
                continue;

            player->fillBlock (numSamples);
            const auto& scratch = player->getScratchBuffer();

            for (int ch = 0; ch < player->getNumChannels(); ++ch)
            {
                const int outIdx = player->getOutputForChannel (ch);

                if (! juce::isPositiveAndBelow (outIdx, juce::jmin (getBusCount (false), numOutputs.load())))
                    continue; // unassigned, or points at a bus the host has disabled

                auto busBuffer = getBusBuffer (buffer, false, outIdx);

                if (busBuffer.getNumChannels() < 1)
                    continue; // bus disabled by host layout negotiation

                // addFrom (not copy) so multiple files can deliberately share
                // the same physical output and mix together.
                busBuffer.addFrom (0, 0, scratch, ch, 0, numSamples);
            }
        }

        playbackPositionSamples += numSamples;

        // Stop automatically once every loaded track has reached its end,
        // instead of letting the transport carry on indefinitely with
        // nothing but silence coming out. Mirrors the Stop button exactly
        // (position rewound to zero) rather than Pause, so the deck is
        // immediately ready to play again from the top.
        const juce::int64 totalLength = longestLoadedLengthSamples.load();

        if (totalLength > 0 && playbackPositionSamples.load() >= totalLength)
            stopPlayback();
    }
}

//==============================================================================
juce::AudioProcessorEditor* TrackDeckAudioProcessor::createEditor()
{
    return new TrackDeckAudioProcessorEditor (*this);
}

//==============================================================================
void TrackDeckAudioProcessor::assignDefaultOutputs (int slot)
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return;

    auto* player = players[slot];
    const int numCh = player->getNumChannels();

    if (numCh <= 0)
        return;

    // Mark every output bus currently used by any OTHER loaded slot.
    const int numOut = numOutputs.load();
    std::vector<bool> used (maxOutputChannels, false);

    for (int i = 0; i < players.size(); ++i)
    {
        if (i == slot)
            continue;

        auto* other = players[i];

        if (! other->isLoaded())
            continue;

        for (int c = 0; c < other->getNumChannels(); ++c)
        {
            const int o = other->getOutputForChannel (c);

            if (juce::isPositiveAndBelow (o, maxOutputChannels))
                used[(size_t) o] = true;
        }
    }

    // Find the first free block of `numCh` outputs. Stereo files only ever
    // search even-aligned starting points (0, 2, 4, ...) so the result is
    // always a valid "1+2" / "3+4" / ... pair - matching what the single
    // Output dropdown actually offers for stereo tracks. Mono files can use
    // any single free output.
    const int step = (numCh == 2) ? 2 : 1;
    int start = -1;

    for (int s = 0; s + numCh <= numOut; s += step)
    {
        bool free = true;

        for (int k = 0; k < numCh; ++k)
        {
            if (used[(size_t) (s + k)])
            {
                free = false;
                break;
            }
        }

        if (free)
        {
            start = s;
            break;
        }
    }

    if (start < 0)
        start = 0; // out of free outputs - fall back to the start (user can fix manually)

    for (int c = 0; c < numCh; ++c)
        player->setOutputForChannel (c, start + c);
}

bool TrackDeckAudioProcessor::loadFileIntoSlot (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return false;

    if (! players[slot]->loadFile (file))
        return false;

    assignDefaultOutputs (slot);
    recomputeLongestLoadedLength();
    notifyHostOfStateChange();
    return true;
}

void TrackDeckAudioProcessor::removeSlot (int slot)
{
    if (juce::isPositiveAndBelow (slot, players.size()))
    {
        players[slot]->clear();
        recomputeLongestLoadedLength();
        notifyHostOfStateChange();
    }
}

void TrackDeckAudioProcessor::setSlotMuted (int slot, bool muted)
{
    if (juce::isPositiveAndBelow (slot, players.size()))
    {
        players[slot]->setMuted (muted);
        notifyHostOfStateChange();
    }
}

bool TrackDeckAudioProcessor::isSlotMuted (int slot) const
{
    return juce::isPositiveAndBelow (slot, players.size()) && players[slot]->isMuted();
}

void TrackDeckAudioProcessor::setSlotVolume (int slot, float volume)
{
    if (juce::isPositiveAndBelow (slot, players.size()))
    {
        players[slot]->setGain (volume);
        notifyHostOfStateChange();
    }
}

float TrackDeckAudioProcessor::getSlotVolume (int slot) const
{
    return juce::isPositiveAndBelow (slot, players.size()) ? players[slot]->getGain() : 1.0f;
}

bool TrackDeckAudioProcessor::isSlotLoaded (int slot) const
{
    return juce::isPositiveAndBelow (slot, players.size()) && players[slot]->isLoaded();
}

juce::String TrackDeckAudioProcessor::getSlotFileName (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, players.size()) || ! players[slot]->isLoaded())
        return {};

    return players[slot]->getFile().getFileName();
}

juce::File TrackDeckAudioProcessor::getSlotFile (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, players.size()) || ! players[slot]->isLoaded())
        return {};

    return players[slot]->getFile();
}

double TrackDeckAudioProcessor::getSlotLengthSeconds (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return 0.0;

    return players[slot]->getLengthInSeconds();
}

int TrackDeckAudioProcessor::getSlotNumChannels (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return 0;

    return players[slot]->getNumChannels();
}

int TrackDeckAudioProcessor::getSlotOutputChannel (int slot, int fileChannel) const
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return -1;

    return players[slot]->getOutputForChannel (fileChannel);
}

void TrackDeckAudioProcessor::setSlotOutputChannel (int slot, int fileChannel, int outputIndex)
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return;

    outputIndex = juce::jlimit (0, numOutputs.load() - 1, outputIndex);
    players[slot]->setOutputForChannel (fileChannel, outputIndex);
    notifyHostOfStateChange();
}

void TrackDeckAudioProcessor::setSlotOutputPair (int slot, int pairIndex)
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return;

    if (players[slot]->getNumChannels() != 2)
        return; // pairs only make sense for stereo files

    const int maxPairIndex = (numOutputs.load() / 2) - 1;
    pairIndex = juce::jlimit (0, maxPairIndex, pairIndex);

    const int left = pairIndex * 2;
    players[slot]->setOutputForChannel (0, left);
    players[slot]->setOutputForChannel (1, left + 1);

    notifyHostOfStateChange();
}

double TrackDeckAudioProcessor::getLongestLoadedLengthSeconds() const
{
    double longest = 0.0;

    for (auto* p : players)
        if (p->isLoaded())
            longest = juce::jmax (longest, p->getLengthInSeconds());

    return longest;
}

void TrackDeckAudioProcessor::recomputeLongestLoadedLength()
{
    // Always called from the message thread (load/remove/state-apply, or
    // prepareToPlay()), so it's fine to read straight from getLongestLoadedLengthSeconds()
    // here - the audio thread never touches FilePlayer's own fields, only
    // the cached atomic this writes.
    const double longestSeconds = getLongestLoadedLengthSeconds();
    longestLoadedLengthSamples.store ((juce::int64) (longestSeconds * currentSampleRate));
}

//==============================================================================
void TrackDeckAudioProcessor::startPlayback()
{
    const double posSeconds = getPlaybackPositionSeconds();

    // Set every loaded slot to exactly the same transport position *before*
    // flagging playback as active, so the very next processBlock() call
    // pulls all of them from the same point in time.
    for (auto* p : players)
        if (p->isLoaded())
            p->setPosition (posSeconds);

    playing = true;
}

void TrackDeckAudioProcessor::pausePlayback()
{
    // Leaves playbackPositionSamples untouched, so a later startPlayback()
    // (or another seekTo()) picks up from exactly where playback left off.
    playing = false;
}

void TrackDeckAudioProcessor::stopPlayback()
{
    playing = false;
    playbackPositionSamples = 0;
}

void TrackDeckAudioProcessor::togglePlayback()
{
    if (playing.load())
        pausePlayback();
    else
        startPlayback();
}

void TrackDeckAudioProcessor::seekTo (double newPositionSeconds)
{
    newPositionSeconds = juce::jmax (0.0, newPositionSeconds);
    playbackPositionSamples = (juce::int64) (newPositionSeconds * currentSampleRate);

    // Reposition every loaded slot immediately (safe whether playing or
    // stopped) so all tracks remain sample-aligned to the new time.
    for (auto* p : players)
        if (p->isLoaded())
            p->setPosition (newPositionSeconds);
}

//==============================================================================
juce::ValueTree TrackDeckAudioProcessor::buildStateTree() const
{
    juce::ValueTree state ("TRACKDECK_STATE");

    state.setProperty ("numOutputs", numOutputs.load(), nullptr);

    for (int i = 0; i < numMidiCommands; ++i)
    {
        state.setProperty (midiCommandKeys[i], midiCcs[(size_t) i].load(), nullptr);
        state.setProperty (midiValueKey (i), midiValues[(size_t) i].load(), nullptr);
    }

    state.setProperty (midiChannelStateKey, midiChannel.load(), nullptr);

    for (int i = 0; i < players.size(); ++i)
    {
        juce::ValueTree track ("TRACK");
        track.setProperty ("index", i, nullptr);
        track.setProperty ("path", players[i]->isLoaded() ? players[i]->getFile().getFullPathName()
                                                            : juce::String(), nullptr);
        track.setProperty ("muted", players[i]->isMuted(), nullptr);
        track.setProperty ("volume", (double) players[i]->getGain(), nullptr);

        juce::String outputsCsv;

        for (int c = 0; c < players[i]->getNumChannels(); ++c)
        {
            if (c > 0)
                outputsCsv << ",";

            outputsCsv << players[i]->getOutputForChannel (c);
        }

        track.setProperty ("outputs", outputsCsv, nullptr);

        state.addChild (track, -1, nullptr);
    }

    return state;
}

void TrackDeckAudioProcessor::applyStateTree (const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    // Must come before any routing is restored, which is validated against it.
    // State without it leaves the current setting alone.
    if (state.hasProperty ("numOutputs"))
        setNumOutputs ((int) state.getProperty ("numOutputs"));

    // State without these properties (saved by an older version, or a
    // hand-written .tracks file with no midi block) leaves the current
    // mapping alone rather than resetting it - it's tied to the user's
    // hardware, not to a particular set of tracks.
    // A number with no value (saved before values existed) gets the default
    // value of 127.
    for (int i = 0; i < numMidiCommands; ++i)
    {
        if (! state.hasProperty (midiCommandKeys[i]))
            continue;

        midiValues[(size_t) i].store (juce::jlimit (0, 127, (int) state.getProperty (midiValueKey (i), defaultMidiValue)));
        setMidiCc ((MidiCommand) i, (int) state.getProperty (midiCommandKeys[i]));
    }

    if (state.hasProperty (midiChannelStateKey))
        setMidiChannel ((int) state.getProperty (midiChannelStateKey));

    for (auto track : state)
    {
        const int index = track.getProperty ("index", -1);

        if (! juce::isPositiveAndBelow (index, players.size()))
            continue;

        const juce::String path = track.getProperty ("path", juce::String());
        bool loaded = false;

        if (path.isNotEmpty())
            loaded = players[index]->loadFile (juce::File (path));

        players[index]->setMuted ((bool) track.getProperty ("muted", false));
        players[index]->setGain ((float) (double) track.getProperty ("volume", 1.0));

        if (loaded)
        {
            const juce::String outputsCsv = track.getProperty ("outputs", juce::String());
            juce::StringArray tokens;
            tokens.addTokens (outputsCsv, ",", "");

            bool restoredOk = tokens.size() == players[index]->getNumChannels();

            if (restoredOk)
            {
                for (int c = 0; c < players[index]->getNumChannels(); ++c)
                {
                    const int outIdx = tokens[c].getIntValue();

                    if (! juce::isPositiveAndBelow (outIdx, numOutputs.load()))
                    {
                        restoredOk = false;
                        break;
                    }

                    players[index]->setOutputForChannel (c, outIdx);
                }
            }

            // Stereo files must land on an adjacent pair (left even, right =
            // left+1) to match the single "1+2, 3+4, ..." Output dropdown -
            // reject anything else (e.g. state saved before this pairing
            // model existed) and fall back to a fresh default assignment.
            if (restoredOk && players[index]->getNumChannels() == 2)
            {
                const int left  = players[index]->getOutputForChannel (0);
                const int right = players[index]->getOutputForChannel (1);

                if (left % 2 != 0 || right != left + 1)
                    restoredOk = false;
            }

            if (! restoredOk)
                assignDefaultOutputs (index);
        }
    }

    recomputeLongestLoadedLength();
}

//==============================================================================
juce::String TrackDeckAudioProcessor::stateTreeToYaml (const juce::ValueTree& state) const
{
    juce::StringArray entries;

    for (auto track : state)
    {
        const juce::String path = track.getProperty ("path", juce::String());

        if (path.isEmpty())
            continue; // skip empty slots - only loaded tracks appear in the file

        juce::String entry;
        entry << "  - path: " << yamlQuoteString (path) << "\n";
        entry << "    muted: " << ((bool) track.getProperty ("muted", false) ? "true" : "false") << "\n";
        entry << "    volume: " << juce::String ((double) track.getProperty ("volume", 1.0), 4) << "\n";

        const juce::String outputsCsv = track.getProperty ("outputs", juce::String());
        juce::StringArray tokens;
        tokens.addTokens (outputsCsv, ",", "");
        entry << "    outputs: [" << tokens.joinIntoString (", ") << "]";

        entries.add (entry);
    }

    juce::String yaml;
    yaml << "# TrackDeck track configuration - saved by TrackDeck, safe to hand-edit\n";

    // How many audio outputs the plugin offers (even, 2-32).
    yaml << "outputs: " << (int) state.getProperty ("numOutputs", defaultNumOutputs) << "\n";

    // MIDI CC numbers (0-127, or "none") that trigger each transport command.
    yaml << "midi:\n";

    for (int i = 0; i < numMidiCommands; ++i)
    {
        const int cc = state.getProperty (midiCommandKeys[i], midiCcUnassigned);
        yaml << "  " << midiCommandKeys[i] << ": " << (cc == midiCcUnassigned ? juce::String ("none") : juce::String (cc)) << "\n";
        yaml << "  " << midiValueKey (i) << ": " << (int) state.getProperty (midiValueKey (i), defaultMidiValue) << "\n";
    }

    // The one MIDI channel all commands listen on: "any" or 1-16.
    const int channel = state.getProperty (midiChannelStateKey, midiChannelAny);
    yaml << "  channel: " << (channel == midiChannelAny ? juce::String ("any") : juce::String (channel)) << "\n";

    yaml << "tracks:";

    if (entries.isEmpty())
        yaml << " []\n";
    else
        yaml << "\n" << entries.joinIntoString ("\n") << "\n";

    return yaml;
}

juce::ValueTree TrackDeckAudioProcessor::yamlToStateTree (const juce::String& yamlText) const
{
    juce::StringArray lines;
    lines.addLines (yamlText);

    int i = 0;

    while (i < lines.size() && ! lines[i].trim().startsWith ("tracks:"))
        ++i;

    if (i >= lines.size())
        return {}; // no "tracks:" key found anywhere - not a recognisable .tracks file

    juce::ValueTree state ("TRACKDECK_STATE");

    // Optional "midi:" block, written above "tracks:" - its "play: 80" style
    // lines can't be mistaken for track keys since they're parsed here.
    for (int m = 0; m < i; ++m)
    {
        const juce::String trimmed = lines[m].trim();
        const int colon = trimmed.indexOfChar (':');

        if (colon <= 0)
            continue;

        const juce::String key = trimmed.substring (0, colon).trim();

        if (key == "outputs")
            state.setProperty ("numOutputs", trimmed.substring (colon + 1).trim().getIntValue(), nullptr);
        else if (key == "channel")
            state.setProperty (midiChannelStateKey, yamlParseMidiChannel (trimmed.substring (colon + 1)), nullptr);

        for (int c = 0; c < numMidiCommands; ++c)
        {
            if (key == midiCommandKeys[c])
                state.setProperty (midiCommandKeys[c], yamlParseMidiCc (trimmed.substring (colon + 1)), nullptr);
            else if (key == midiValueKey (c))
                state.setProperty (midiValueKey (c), juce::jlimit (0, 127, trimmed.substring (colon + 1).getIntValue()), nullptr);
        }
    }

    ++i; // move past the "tracks:" line itself

    int trackIndex = 0;
    juce::ValueTree currentTrack;

    for (; i < lines.size() && trackIndex < maxTracks; ++i)
    {
        juce::String trimmed = lines[i].trim();

        if (trimmed.isEmpty() || trimmed.startsWithChar ('#'))
            continue;

        if (trimmed.startsWith ("- "))
        {
            if (currentTrack.isValid())
                state.addChild (currentTrack, -1, nullptr);

            currentTrack = juce::ValueTree ("TRACK");
            currentTrack.setProperty ("index", trackIndex, nullptr);
            ++trackIndex;

            trimmed = trimmed.substring (2).trim(); // first key may share the "- " line
        }

        if (! currentTrack.isValid())
            continue; // stray content before any "- " list item - ignore

        const int colon = trimmed.indexOfChar (':');

        if (colon <= 0)
            continue;

        const juce::String key   = trimmed.substring (0, colon).trim();
        const juce::String value = trimmed.substring (colon + 1).trim();

        if (key == "path")
            currentTrack.setProperty ("path", yamlUnquoteString (value), nullptr);
        else if (key == "muted")
            currentTrack.setProperty ("muted", value.equalsIgnoreCase ("true"), nullptr);
        else if (key == "volume")
            currentTrack.setProperty ("volume", value.getDoubleValue(), nullptr);
        else if (key == "outputs")
            currentTrack.setProperty ("outputs", yamlIntListToCsv (value), nullptr);
    }

    if (currentTrack.isValid())
        state.addChild (currentTrack, -1, nullptr);

    return state;
}

bool TrackDeckAudioProcessor::saveTracksToFile (const juce::File& file) const
{
    return file.replaceWithText (stateTreeToYaml (buildStateTree()));
}

bool TrackDeckAudioProcessor::loadTracksFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto parsedState = yamlToStateTree (file.loadFileAsString());

    if (! parsedState.isValid())
        return false; // not a recognisable .tracks file - nothing was changed

    // Full replace, not a merge: clear every slot before applying whatever
    // the file describes (which may itself be an intentionally empty list).
    for (auto* p : players)
        p->clear();

    applyStateTree (parsedState);
    notifyHostOfStateChange();
    return true;
}

//==============================================================================
void TrackDeckAudioProcessor::notifyHostOfStateChange()
{
    juce::AudioProcessorListener::ChangeDetails details;
    details.nonParameterStateChanged = true;
    updateHostDisplay (details);
}

//==============================================================================
void TrackDeckAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = buildStateTree();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TrackDeckAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr)
        return;

    applyStateTree (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates the actual plugin instance for the host.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrackDeckAudioProcessor();
}

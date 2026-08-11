#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor::BusesProperties TrackDeckAudioProcessor::buildBusesProperties()
{
    BusesProperties props;

    for (int i = 0; i < maxOutputChannels; ++i)
        props = props.withOutput (busNameForOutput (i), juce::AudioChannelSet::mono(), true);

    return props;
}

TrackDeckAudioProcessor::TrackDeckAudioProcessor()
    : AudioProcessor (buildBusesProperties())
{
    // Registers WAV, AIFF, FLAC and (with JUCE_USE_MP3AUDIOFORMAT=1) MP3 decoding.
    formatManager.registerBasicFormats();

    for (int i = 0; i < maxTracks; ++i)
        players.add (new FilePlayer (formatManager));

    // Restore whatever was loaded last time, independent of any DAW project -
    // see the class comment for how this interacts with host state restore.
    loadPersistedSettings();
}

TrackDeckAudioProcessor::~TrackDeckAudioProcessor() = default;

//==============================================================================
void TrackDeckAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    for (auto* p : players)
        p->prepare (sampleRate, samplesPerBlock);
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

void TrackDeckAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

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

                if (! juce::isPositiveAndBelow (outIdx, getBusCount (false)))
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

    for (int s = 0; s + numCh <= maxOutputChannels; s += step)
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
    savePersistedSettings();
    notifyHostOfStateChange();
    return true;
}

void TrackDeckAudioProcessor::removeSlot (int slot)
{
    if (juce::isPositiveAndBelow (slot, players.size()))
    {
        players[slot]->clear();
        savePersistedSettings();
        notifyHostOfStateChange();
    }
}

void TrackDeckAudioProcessor::setSlotMuted (int slot, bool muted)
{
    if (juce::isPositiveAndBelow (slot, players.size()))
    {
        players[slot]->setMuted (muted);
        savePersistedSettings();
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
        savePersistedSettings();
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

    outputIndex = juce::jlimit (0, maxOutputChannels - 1, outputIndex);
    players[slot]->setOutputForChannel (fileChannel, outputIndex);
    savePersistedSettings();
    notifyHostOfStateChange();
}

void TrackDeckAudioProcessor::setSlotOutputPair (int slot, int pairIndex)
{
    if (! juce::isPositiveAndBelow (slot, players.size()))
        return;

    if (players[slot]->getNumChannels() != 2)
        return; // pairs only make sense for stereo files

    const int maxPairIndex = (maxOutputChannels / 2) - 1;
    pairIndex = juce::jlimit (0, maxPairIndex, pairIndex);

    const int left = pairIndex * 2;
    players[slot]->setOutputForChannel (0, left);
    players[slot]->setOutputForChannel (1, left + 1);

    savePersistedSettings();
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

                    if (! juce::isPositiveAndBelow (outIdx, maxOutputChannels))
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
}

//==============================================================================
juce::File TrackDeckAudioProcessor::getPersistedSettingsFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("TrackDeck");

    dir.createDirectory();

    return dir.getChildFile ("last_session.xml");
}

void TrackDeckAudioProcessor::savePersistedSettings() const
{
    auto state = buildStateTree();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        xml->writeTo (getPersistedSettingsFile());
}

void TrackDeckAudioProcessor::notifyHostOfStateChange()
{
    juce::AudioProcessorListener::ChangeDetails details;
    details.nonParameterStateChanged = true;
    updateHostDisplay (details);
}

void TrackDeckAudioProcessor::loadPersistedSettings()
{
    auto file = getPersistedSettingsFile();

    if (! file.existsAsFile())
        return;

    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));

    if (xml == nullptr)
        return;

    applyStateTree (juce::ValueTree::fromXml (*xml));
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

    // A DAW project was just loaded with its own saved state for this
    // plugin instance - treat that as the new "last used" settings too, so
    // the two persistence mechanisms stay in sync.
    savePersistedSettings();
}

//==============================================================================
// This creates the actual plugin instance for the host.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrackDeckAudioProcessor();
}

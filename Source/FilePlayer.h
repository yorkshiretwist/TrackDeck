#pragma once

#include <JuceHeader.h>

/**
    Wraps a single loaded audio file: decoding (via AudioFormatReaderSource),
    sample-rate conversion to the host's rate (via ResamplingAudioSource),
    mute state, volume (gain), transport position, and per-channel output
    routing.

    A file can be mono (1 channel) or stereo (2 channels) — anything with
    more channels has only its first 2 channels used. Each of those 1-2
    channels can independently be routed to any of the plugin's mono output
    buses via setOutputForChannel(), so a stereo file can feed two separate
    physical outputs and a mono file feeds just one.
*/
class FilePlayer
{
public:
    static constexpr int maxChannelsPerFile = 2;

    explicit FilePlayer (juce::AudioFormatManager& formatManagerToUse)
        : formatManager (formatManagerToUse)
    {
    }

    ~FilePlayer()
    {
        releaseResources();
    }

    /** Loads (or replaces) the file played by this slot. Returns false if the
        format manager could not find a decoder for it. Resets any previous
        output-channel assignment to "unassigned" (-1) and volume back to
        unity (1.0); the caller (the processor) is expected to assign
        sensible output defaults right after this returns true, and restore
        a saved volume if there is one. */
    bool loadFile (const juce::File& fileToLoad)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (fileToLoad));

        if (reader == nullptr)
            return false;

        const juce::ScopedLock sl (lock);

        file = fileToLoad;
        sourceSampleRate = reader->sampleRate;
        lengthInSamples = reader->lengthInSamples;
        numChannels = juce::jlimit (1, maxChannelsPerFile, (int) reader->numChannels);

        readerSource.reset (new juce::AudioFormatReaderSource (reader.release(), true));
        resamplingSource.reset (new juce::ResamplingAudioSource (readerSource.get(), false, numChannels));

        outputAssignment.assign ((size_t) numChannels, -1);
        gain.store (1.0f);

        if (preparedSampleRate > 0.0)
            prepare (preparedSampleRate, preparedBlockSize);

        return true;
    }

    /** Removes the currently loaded file, if any. */
    void clear()
    {
        const juce::ScopedLock sl (lock);
        resamplingSource.reset();
        readerSource.reset();
        file = juce::File();
        lengthInSamples = 0;
        numChannels = 0;
        outputAssignment.clear();
        scratchBuffer.setSize (0, 0);
    }

    void prepare (double sampleRate, int blockSize)
    {
        const juce::ScopedLock sl (lock);

        preparedSampleRate = sampleRate;
        preparedBlockSize = blockSize;

        if (resamplingSource != nullptr)
        {
            resamplingSource->setResamplingRatio (sourceSampleRate / sampleRate);
            resamplingSource->prepareToPlay (blockSize, sampleRate);
            scratchBuffer.setSize (juce::jmax (1, numChannels), juce::jmax (1, blockSize), false, false, true);
        }
    }

    void releaseResources()
    {
        const juce::ScopedLock sl (lock);

        if (resamplingSource != nullptr)
            resamplingSource->releaseResources();
    }

    /** Moves the read position to the given time (seconds), measured in the
        *source file's* timeline. All slots are given the same value so that
        playback starts perfectly in sync regardless of each file's native
        sample rate. */
    void setPosition (double newPositionSeconds)
    {
        const juce::ScopedLock sl (lock);

        if (readerSource != nullptr)
            readerSource->setNextReadPosition ((juce::int64) (newPositionSeconds * sourceSampleRate));
    }

    /** Decodes the next `numSamples` into the internal scratch buffer (silence
        if nothing loaded). Read the result via getScratchBuffer(). Mute is
        applied by silencing the output AFTER reading, not by skipping the
        read - if we skipped it, this track's position would stop advancing
        while muted and it would come back out of sync with the other
        tracks the moment it's unmuted. Volume is applied every block
        regardless of mute state, so unmuting always reflects the current
        fader position. */
    void fillBlock (int numSamples)
    {
        const juce::ScopedLock sl (lock);

        if (resamplingSource == nullptr)
        {
            scratchBuffer.clear (0, numSamples);
            return;
        }

        juce::AudioSourceChannelInfo info (&scratchBuffer, 0, numSamples);
        resamplingSource->getNextAudioBlock (info); // always advances position, muted or not

        scratchBuffer.applyGain (0, numSamples, gain.load());

        if (muted.load())
            scratchBuffer.clear (0, numSamples); // silence the output only, position stays advanced
    }

    const juce::AudioBuffer<float>& getScratchBuffer() const { return scratchBuffer; }

    void setMuted (bool shouldBeMuted) { muted.store (shouldBeMuted); }
    bool isMuted() const                { return muted.load(); }

    /** Linear gain, 0.0 (silent) to 1.0 (unity/full volume). */
    void setGain (float newGain) { gain.store (juce::jlimit (0.0f, 1.0f, newGain)); }
    float getGain() const         { return gain.load(); }

    bool isLoaded() const          { return readerSource != nullptr; }
    juce::File getFile() const     { return file; }
    int getNumChannels() const     { return numChannels; }

    /** Which plugin output bus (0-based) a given file channel is routed to,
        or -1 if unassigned. */
    int getOutputForChannel (int channel) const
    {
        return juce::isPositiveAndBelow (channel, (int) outputAssignment.size()) ? outputAssignment[(size_t) channel] : -1;
    }

    void setOutputForChannel (int channel, int outputIndex)
    {
        if (juce::isPositiveAndBelow (channel, (int) outputAssignment.size()))
            outputAssignment[(size_t) channel] = outputIndex;
    }

    double getLengthInSeconds() const
    {
        return sourceSampleRate > 0.0 ? (double) lengthInSamples / sourceSampleRate : 0.0;
    }

private:
    juce::AudioFormatManager& formatManager;
    juce::CriticalSection lock;

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::ResamplingAudioSource>    resamplingSource;
    juce::AudioBuffer<float> scratchBuffer;

    juce::File file;
    double sourceSampleRate = 44100.0;
    juce::int64 lengthInSamples = 0;
    int numChannels = 0;

    std::vector<int> outputAssignment; // one entry per channel; value = output bus index, or -1
    std::atomic<float> gain { 1.0f };

    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;

    std::atomic<bool> muted { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilePlayer)
};

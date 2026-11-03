#pragma once

#include <array>
#include <atomic>

namespace trackdeck
{
    /** The transport actions that can be triggered by a MIDI CC. */
    enum class MidiCommand { play = 0, pause, stop, toggle };
    constexpr int numMidiCommands = 4;

    /** CC number meaning "no CC assigned" for a command. */
    constexpr int midiCcUnassigned = -1;

    /** Default CC value for every command: a switch's "on" value. */
    constexpr int defaultMidiValue = 127;

    /** MIDI channel setting meaning "respond on any channel"; otherwise a
        channel number from 1 to 16. This is the default. */
    constexpr int midiChannelAny = 0;

    /** Default CC for each command (General Purpose Controller 5-8, which
        are on/off switch-type CCs that nothing sends unprompted). */
    constexpr int defaultMidiCc (MidiCommand command) { return 80 + (int) command; }

    inline int clampInt (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    /** What the player should actually do for a command. */
    enum class TransportAction { none, play, pause, stop, toggle };

    /** A command asking for the state the transport is already in is ignored.
        "Paused" and "stopped" are both not-playing; they differ only in
        whether the position has been rewound to zero. */
    inline TransportAction resolveTransportAction (MidiCommand command, bool isPlaying, bool isAtStart)
    {
        switch (command)
        {
            case MidiCommand::play:   return isPlaying ? TransportAction::none : TransportAction::play;
            case MidiCommand::pause:  return isPlaying ? TransportAction::pause : TransportAction::none;
            case MidiCommand::stop:   return (isPlaying || ! isAtStart) ? TransportAction::stop : TransportAction::none;
            case MidiCommand::toggle: return TransportAction::toggle;
        }

        return TransportAction::none;
    }

    /**
        MIDI transport control settings and matching. Pure C++ (no JUCE), so it
        can be unit tested without a host.

        Each command is triggered by one exact CC number AND one exact CC value,
        on the one MIDI channel shared by all commands (any channel by default,
        or one chosen channel 1-16); a CC message with the same number but a
        different value (such as the 0 a footswitch sends on release) or on
        another channel does nothing.

        Every field is an independent atomic so the UI thread can edit it while
        the audio thread is running, with no locks.
    */
    class MidiControl
    {
    public:
        MidiControl() { resetToDefaults(); }

        /** Resets every command to its default CC number and value 127, and the
            channel to any. Does not change MIDI learn. */
        void resetToDefaults()
        {
            for (int i = 0; i < numMidiCommands; ++i)
            {
                ccs[(size_t) i].store (defaultMidiCc ((MidiCommand) i));
                values[(size_t) i].store (defaultMidiValue);
            }

            channel.store (midiChannelAny);
        }

        /** The CC number (0-127) assigned to `command`, or midiCcUnassigned. */
        int getCc (MidiCommand command) const { return ccs[(size_t) command].load(); }

        /** The CC value (0-127) that `command` waits for. */
        int getValue (MidiCommand command) const { return values[(size_t) command].load(); }

        /** The MIDI channel all commands listen on: midiChannelAny, or 1-16. */
        int getChannel() const { return channel.load(); }

        /** Sets the channel all commands listen on; anything outside 0-16 means any. */
        void setChannel (int newChannel)
        {
            channel.store (newChannel >= 0 && newChannel <= 16 ? newChannel : midiChannelAny);
        }

        /** Assigns a CC number (0-127; anything else means unassigned). */
        void setCc (MidiCommand command, int cc)
        {
            if (cc < 0 || cc > 127)
                cc = midiCcUnassigned;

            ccs[(size_t) command].store (cc);
            unassignDuplicatesOf (command);
        }

        /** Sets the CC value `command` waits for, clamped to 0-127. */
        void setValue (MidiCommand command, int value)
        {
            values[(size_t) command].store (clampInt (value, 0, 127));
            unassignDuplicatesOf (command);
        }

        /** Sets a command's CC number and value together (as when restoring saved
            state), then unassigns any other command with the identical pair. */
        void setCcAndValue (MidiCommand command, int cc, int value)
        {
            ccs[(size_t) command].store (cc < 0 || cc > 127 ? midiCcUnassigned : cc);
            values[(size_t) command].store (clampInt (value, 0, 127));
            unassignDuplicatesOf (command);
        }

        /** Arms MIDI learn: the next CC message received (any value, any
            channel) sets both the number and the value of `command`; the channel
            setting is left as it is. Arming a different command replaces the
            previous request. */
        void beginLearn (MidiCommand command) { learnTarget.store ((int) command); }
        void cancelLearn() { learnTarget.store (-1); }

        /** The command index waiting for a CC, or -1 if MIDI learn isn't armed
            (including after it has just captured a CC). */
        int getLearnTarget() const { return learnTarget.load(); }

        /** Applies one incoming CC message: completes MIDI learn if armed,
            otherwise calls `onCommand (MidiCommand)` for each command the CC is
            assigned to. `ccChannel` is 1-16. Returns true if the message
            completed a MIDI learn (it is consumed, not also acted on). */
        template <class OnCommand>
        bool handleCc (int ccChannel, int cc, int value, OnCommand&& onCommand)
        {
            if (cc < 0 || cc > 127)
                return false;

            const int learning = learnTarget.load();

            if (learning >= 0)
            {
                // It sets the value as well as the number, so whatever the
                // control sends when pressed is exactly what will trigger the command.
                ccs[(size_t) learning].store (cc);
                values[(size_t) learning].store (clampInt (value, 0, 127));
                unassignDuplicatesOf ((MidiCommand) learning);
                learnTarget.store (-1);
                return true;
            }

            const int wantedChannel = channel.load();

            if (wantedChannel != midiChannelAny && wantedChannel != ccChannel)
                return false;

            for (int i = 0; i < numMidiCommands; ++i)
                if (ccs[(size_t) i].load() == cc && values[(size_t) i].load() == value)
                    onCommand ((MidiCommand) i);

            return false;
        }

    private:
        /** After `command`'s number or value changed: any other command with
            the identical number AND value is unassigned, so one CC message can
            never fire two commands. (Different values on the same number are
            fine.) */
        void unassignDuplicatesOf (MidiCommand command)
        {
            const int cc = ccs[(size_t) command].load();

            if (cc == midiCcUnassigned)
                return;

            const int value = values[(size_t) command].load();

            for (size_t i = 0; i < ccs.size(); ++i)
                if (i != (size_t) command && ccs[i].load() == cc && values[i].load() == value)
                    ccs[i].store (midiCcUnassigned);
        }

        std::array<std::atomic<int>, numMidiCommands> ccs;    // CC number per MidiCommand, or midiCcUnassigned
        std::array<std::atomic<int>, numMidiCommands> values; // CC value per MidiCommand
        std::atomic<int> channel { midiChannelAny };           // the channel all commands listen on: midiChannelAny or 1-16
        std::atomic<int> learnTarget { -1 };                   // MidiCommand index awaiting a CC, or -1
    };
}

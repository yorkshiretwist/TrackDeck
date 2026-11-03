#include <catch2/catch_test_macros.hpp>

#include "Engine/MidiControl.h"

#include <vector>

using namespace trackdeck;

namespace
{
    /** Feeds one CC message and returns the commands it fired. */
    struct Result
    {
        bool learned = false;
        std::vector<MidiCommand> fired;
    };

    Result send (MidiControl& m, int channel, int cc, int value)
    {
        Result r;
        r.learned = m.handleCc (channel, cc, value, [&] (MidiCommand c) { r.fired.push_back (c); });
        return r;
    }

    bool firedOnly (const Result& r, MidiCommand c)
    {
        return ! r.learned && r.fired.size() == 1 && r.fired[0] == c;
    }

    bool firedNothing (const Result& r) { return ! r.learned && r.fired.empty(); }
}

TEST_CASE ("Defaults: CC 80-83, value 127, any channel", "[defaults]")
{
    MidiControl m;

    CHECK (m.getCc (MidiCommand::play)   == 80);
    CHECK (m.getCc (MidiCommand::pause)  == 81);
    CHECK (m.getCc (MidiCommand::stop)   == 82);
    CHECK (m.getCc (MidiCommand::toggle) == 83);

    for (int i = 0; i < numMidiCommands; ++i)
        CHECK (m.getValue ((MidiCommand) i) == 127);

    CHECK (m.getChannel() == midiChannelAny);
    CHECK (m.getLearnTarget() == -1);
}

TEST_CASE ("resetToDefaults restores every setting", "[defaults]")
{
    MidiControl m;
    m.setCc (MidiCommand::play, 10);
    m.setValue (MidiCommand::play, 5);
    m.setChannel (7);

    m.resetToDefaults();

    CHECK (m.getCc (MidiCommand::play) == 80);
    CHECK (m.getValue (MidiCommand::play) == 127);
    CHECK (m.getChannel() == midiChannelAny);
}

TEST_CASE ("Each default CC fires its own command", "[matching]")
{
    MidiControl m;

    CHECK (firedOnly (send (m, 1, 80, 127), MidiCommand::play));
    CHECK (firedOnly (send (m, 1, 81, 127), MidiCommand::pause));
    CHECK (firedOnly (send (m, 1, 82, 127), MidiCommand::stop));
    CHECK (firedOnly (send (m, 1, 83, 127), MidiCommand::toggle));
}

TEST_CASE ("A different value on the same CC does nothing (e.g. a footswitch release)", "[matching]")
{
    MidiControl m;

    CHECK (firedNothing (send (m, 1, 80, 0)));
    CHECK (firedNothing (send (m, 1, 80, 126)));
}

TEST_CASE ("An unassigned or unrelated CC does nothing", "[matching]")
{
    MidiControl m;

    CHECK (firedNothing (send (m, 1, 7, 127)));

    m.setCc (MidiCommand::play, midiCcUnassigned);
    CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned);
    CHECK (firedNothing (send (m, 1, 80, 127)));
}

TEST_CASE ("Out of range CC numbers are ignored", "[matching]")
{
    MidiControl m;

    CHECK (firedNothing (send (m, 1, -1, 127)));
    CHECK (firedNothing (send (m, 1, 128, 127)));
}

TEST_CASE ("Any channel matches all 16 channels", "[matching][channel]")
{
    MidiControl m;

    for (int ch = 1; ch <= 16; ++ch)
        CHECK (firedOnly (send (m, ch, 80, 127), MidiCommand::play));
}

TEST_CASE ("A chosen channel only matches that channel", "[matching][channel]")
{
    MidiControl m;
    m.setChannel (3);

    CHECK (firedOnly (send (m, 3, 80, 127), MidiCommand::play));
    CHECK (firedNothing (send (m, 1, 80, 127)));
    CHECK (firedNothing (send (m, 4, 80, 127)));
}

TEST_CASE ("Channel setting is validated", "[settings][channel]")
{
    MidiControl m;

    m.setChannel (16);
    CHECK (m.getChannel() == 16);

    m.setChannel (17);
    CHECK (m.getChannel() == midiChannelAny);

    m.setChannel (5);
    m.setChannel (-1);
    CHECK (m.getChannel() == midiChannelAny);

    m.setChannel (0);
    CHECK (m.getChannel() == midiChannelAny);
}

TEST_CASE ("CC number and value are validated", "[settings]")
{
    MidiControl m;

    m.setCc (MidiCommand::stop, 127);
    CHECK (m.getCc (MidiCommand::stop) == 127);

    m.setCc (MidiCommand::stop, 128);
    CHECK (m.getCc (MidiCommand::stop) == midiCcUnassigned);

    m.setCc (MidiCommand::stop, -5);
    CHECK (m.getCc (MidiCommand::stop) == midiCcUnassigned);

    m.setCc (MidiCommand::stop, 0);
    CHECK (m.getCc (MidiCommand::stop) == 0);

    m.setValue (MidiCommand::stop, 200);
    CHECK (m.getValue (MidiCommand::stop) == 127);

    m.setValue (MidiCommand::stop, -3);
    CHECK (m.getValue (MidiCommand::stop) == 0);
}

TEST_CASE ("A custom CC and value fire the command", "[matching]")
{
    MidiControl m;
    m.setCc (MidiCommand::play, 20);
    m.setValue (MidiCommand::play, 64);

    CHECK (firedOnly (send (m, 1, 20, 64), MidiCommand::play));
    CHECK (firedNothing (send (m, 1, 20, 127)));
    CHECK (firedNothing (send (m, 1, 80, 127))); // the old default no longer plays
}

TEST_CASE ("The same CC with different values can drive different commands", "[matching]")
{
    MidiControl m;
    m.setCcAndValue (MidiCommand::play, 20, 127);
    m.setCcAndValue (MidiCommand::stop, 20, 0);

    CHECK (m.getCc (MidiCommand::play) == 20);
    CHECK (m.getCc (MidiCommand::stop) == 20);
    CHECK (firedOnly (send (m, 1, 20, 127), MidiCommand::play));
    CHECK (firedOnly (send (m, 1, 20, 0), MidiCommand::stop));
}

TEST_CASE ("Assigning an identical CC and value unassigns the other command", "[duplicates]")
{
    MidiControl m;

    SECTION ("by setting the CC number")
    {
        m.setCc (MidiCommand::pause, 80); // play already has 80 / 127

        CHECK (m.getCc (MidiCommand::pause) == 80);
        CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned);
        CHECK (firedOnly (send (m, 1, 80, 127), MidiCommand::pause));
    }

    SECTION ("by setting the value")
    {
        m.setValue (MidiCommand::play, 10);
        m.setCc (MidiCommand::pause, 80); // same number as play, different value: both keep it
        CHECK (m.getCc (MidiCommand::play) == 80);

        m.setValue (MidiCommand::pause, 10); // now identical: pause wins

        CHECK (m.getCc (MidiCommand::pause) == 80);
        CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned);
    }

    SECTION ("an unassigned command never counts as a duplicate")
    {
        m.setCc (MidiCommand::play, midiCcUnassigned);
        m.setCc (MidiCommand::pause, midiCcUnassigned);

        CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned);
        CHECK (m.getCc (MidiCommand::pause) == midiCcUnassigned);
        CHECK (m.getCc (MidiCommand::stop) == 82);
    }

    SECTION ("setCcAndValue unassigns the other command too")
    {
        m.setCcAndValue (MidiCommand::stop, 80, 127);

        CHECK (m.getCc (MidiCommand::stop) == 80);
        CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned);
    }
}

TEST_CASE ("setCcAndValue validates both, and does not disturb others on the way", "[settings]")
{
    MidiControl m;

    // Restoring saved state: moving play to 81 / 10 must not unassign pause (81 / 127).
    m.setCcAndValue (MidiCommand::play, 81, 10);

    CHECK (m.getCc (MidiCommand::play) == 81);
    CHECK (m.getValue (MidiCommand::play) == 10);
    CHECK (m.getCc (MidiCommand::pause) == 81);
    CHECK (m.getValue (MidiCommand::pause) == 127);

    m.setCcAndValue (MidiCommand::play, 300, 300);
    CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned);
    CHECK (m.getValue (MidiCommand::play) == 127);
}

TEST_CASE ("MIDI learn captures the first CC and its value, and consumes it", "[learn]")
{
    MidiControl m;
    m.beginLearn (MidiCommand::stop);
    CHECK (m.getLearnTarget() == (int) MidiCommand::stop);

    const auto r = send (m, 5, 30, 99);

    CHECK (r.learned);
    CHECK (r.fired.empty());
    CHECK (m.getCc (MidiCommand::stop) == 30);
    CHECK (m.getValue (MidiCommand::stop) == 99);
    CHECK (m.getLearnTarget() == -1);

    // The channel setting is left as it was.
    CHECK (m.getChannel() == midiChannelAny);
}

TEST_CASE ("MIDI learn consumes the message even when it matches another command", "[learn]")
{
    MidiControl m;
    m.beginLearn (MidiCommand::stop);

    const auto r = send (m, 1, 80, 127); // play's default

    CHECK (r.learned);
    CHECK (r.fired.empty());
    CHECK (m.getCc (MidiCommand::stop) == 80);
    CHECK (m.getCc (MidiCommand::play) == midiCcUnassigned); // duplicate removed
}

TEST_CASE ("MIDI learn captures from any channel, even when a channel is set", "[learn][channel]")
{
    MidiControl m;
    m.setChannel (2);
    m.beginLearn (MidiCommand::play);

    CHECK (send (m, 9, 40, 1).learned);
    CHECK (m.getCc (MidiCommand::play) == 40);
    CHECK (m.getValue (MidiCommand::play) == 1);
    CHECK (m.getChannel() == 2);
}

TEST_CASE ("MIDI learn disarms after one capture", "[learn]")
{
    MidiControl m;
    m.beginLearn (MidiCommand::play);
    send (m, 1, 40, 64);

    const auto r = send (m, 1, 40, 64);

    CHECK (! r.learned);
    CHECK (firedOnly (r, MidiCommand::play)); // now acts as a normal trigger
}

TEST_CASE ("MIDI learn can be cancelled", "[learn]")
{
    MidiControl m;
    m.beginLearn (MidiCommand::play);
    m.cancelLearn();
    CHECK (m.getLearnTarget() == -1);

    const auto r = send (m, 1, 40, 64);

    CHECK (! r.learned);
    CHECK (m.getCc (MidiCommand::play) == 80);
}

TEST_CASE ("Arming a different command replaces the previous learn request", "[learn]")
{
    MidiControl m;
    m.beginLearn (MidiCommand::play);
    m.beginLearn (MidiCommand::toggle);

    send (m, 1, 40, 64);

    CHECK (m.getCc (MidiCommand::toggle) == 40);
    CHECK (m.getCc (MidiCommand::play) == 80);
}

TEST_CASE ("Out of range CC numbers do not complete MIDI learn", "[learn]")
{
    MidiControl m;
    m.beginLearn (MidiCommand::play);

    CHECK (! send (m, 1, 200, 64).learned);
    CHECK (m.getLearnTarget() == (int) MidiCommand::play);
}

TEST_CASE ("Transport rules ignore commands asking for the current state", "[transport]")
{
    // play: only when not already playing
    CHECK (resolveTransportAction (MidiCommand::play, false, true)  == TransportAction::play);
    CHECK (resolveTransportAction (MidiCommand::play, false, false) == TransportAction::play);
    CHECK (resolveTransportAction (MidiCommand::play, true,  false) == TransportAction::none);

    // pause: only while playing
    CHECK (resolveTransportAction (MidiCommand::pause, true,  false) == TransportAction::pause);
    CHECK (resolveTransportAction (MidiCommand::pause, false, false) == TransportAction::none);
    CHECK (resolveTransportAction (MidiCommand::pause, false, true)  == TransportAction::none);

    // stop: while playing, or paused part-way; not when already stopped at the start
    CHECK (resolveTransportAction (MidiCommand::stop, true,  false) == TransportAction::stop);
    CHECK (resolveTransportAction (MidiCommand::stop, false, false) == TransportAction::stop);
    CHECK (resolveTransportAction (MidiCommand::stop, true,  true)  == TransportAction::stop);
    CHECK (resolveTransportAction (MidiCommand::stop, false, true)  == TransportAction::none);

    // toggle always acts
    CHECK (resolveTransportAction (MidiCommand::toggle, true,  false) == TransportAction::toggle);
    CHECK (resolveTransportAction (MidiCommand::toggle, false, true)  == TransportAction::toggle);
}

#pragma once

#include <JuceHeader.h>

/**
    Every visual styling constant the editor uses - colours, font sizes, and
    key layout spacing - lives here, grouped by the UI element it affects.
    This is the ONLY file you should need to touch to change how the plugin
    looks: recolour a button, tweak a border, adjust the gap between tracks,
    etc. Nothing in here affects audio behaviour.

    Colours use 0xAARRGGBB hex (JUCE's Colour(uint32) constructor) except
    where an alpha needs to be layered onto a base colour, which uses
    Colour::withAlpha() instead so the base RGB stays easy to compare against
    a design spec given as separate "colour" + "opacity" values.
*/
namespace Theme
{
    //==============================================================================
    // Overall plugin window
    namespace Window
    {
        inline const juce::Colour backgroundColour { 0xff0b0d12 };

        inline const juce::Colour titleTextColour { juce::Colours::white };
        constexpr float titleFontSizePx = 18.0f;
    }

    //==============================================================================
    // The scrollable list that holds all track rows
    namespace TrackList
    {
        inline const juce::Colour backgroundColour { 0xff0f172a }; // shows through in the gap between rows (see rowGapPx)

        constexpr int rowHeightPx = 88; // total height allotted to each track row, gap included - tall enough for the volume slider plus the mute/output/delete row below it
        constexpr int rowGapPx    = 6;  // vertical space between adjacent track rows
    }

    //==============================================================================
    // An individual track row: number label, waveform, name/length overlay.
    // (Mute/delete buttons and the output dropdowns have their own sections
    // below, since they're reused UI components in their own right.)
    namespace TrackRow
    {
        inline const juce::Colour backgroundColour { 0xff0f172a }; // the row's own card background - kept as a separate value from TrackList::backgroundColour so the two can be tweaked independently (e.g. to make rows a lighter "card" against a darker list background)

        inline const juce::Colour numberTextColour { juce::Colours::white };
        constexpr float numberFontSizePx = 16.0f;

        inline const juce::Colour nameTextColour   { juce::Colours::white };
        inline const juce::Colour lengthTextColour { juce::Colours::lightgrey };
        constexpr float lengthFontSizePx = 12.0f;

        // Semi-transparent backdrop drawn tightly behind the file-name text
        // only, so it stays readable over the waveform.
        inline const juce::Colour nameBackdropColour { juce::Colours::black.withAlpha (0.55f) };

        // Reserved pixel widths on each side of the row for the track-number
        // label (left) and the mute/delete/output controls (right); the
        // waveform fills whatever's left in between.
        constexpr int leftMarginPx  = 36;
        constexpr int rightMarginPx = 210;
    }

    //==============================================================================
    // The waveform drawn inside each track row
    namespace Waveform
    {
        inline const juce::Colour peaksColour       { 0xff41d67c }; // normal (unmuted)
        inline const juce::Colour mutedPeaksColour  { 0xff6f6f6f }; // greyed out while muted
        inline const juce::Colour leftBorderColour  { juce::Colours::white.withAlpha (0.25f) }; // thin marker at the waveform's start (time zero)
        inline const juce::Colour playheadColour    { juce::Colours::white };
    }

    //==============================================================================
    // Per-track volume slider (spans the full width of the right-hand column)
    namespace VolumeSlider
    {
        inline const juce::Colour filledTrackColour   { 0xff6366f1 }; // portion of the track up to the current value
        inline const juce::Colour unfilledTrackColour { 0xff26262b }; // remainder of the track
        inline const juce::Colour thumbColour          { juce::Colours::white };

        constexpr int heightPx           = 20; // slider's own height
        constexpr int horizontalInsetPx  = 2;  // inset from the row's right-hand column edges
        constexpr int gapBelowPx         = 2;  // vertical space between the slider and the mute/output/delete row beneath it
    }

    //==============================================================================
    // Mute toggle button
    namespace MuteButton
    {
        inline const juce::Colour borderColour { 0xff6366f1 }; // box outline when untoggled
        inline const juce::Colour tickColour   { 0xff6366f1 }; // check mark when toggled on
        inline const juce::Colour textColour   { juce::Colours::white };
    }

    //==============================================================================
    // Delete/remove ("X") button - a small borderless icon button
    namespace DeleteButton
    {
        inline const juce::Colour normalColour { 0xffb84a4a };
        inline const juce::Colour overColour   { 0xffd66565 };
        inline const juce::Colour downColour   { 0xff8a3535 };

        constexpr int sizePx = 22;
    }

    //==============================================================================
    // Output-routing dropdown (the single "Output" combo box on each row)
    namespace ChannelDropdown
    {
        inline const juce::Colour backgroundColour { 0xff0f172a };
        inline const juce::Colour borderColour     { juce::Colour (0xff6366f1).withAlpha (0.6f) };
        inline const juce::Colour textColour       { juce::Colours::white };

        inline const juce::Colour tagLabelColour { juce::Colours::lightgrey }; // the small "Output" label beside the dropdown
        constexpr float tagFontSizePx = 11.0f;
    }

    //==============================================================================
    // "+ Add Track" button
    namespace AddTrackButton
    {
        inline const juce::Colour backgroundColour { 0xff6366f1 };
        inline const juce::Colour textColour       { juce::Colours::white };
    }

    //==============================================================================
    // Secondary bottom-row buttons (Save / Load .tracks file) - a more
    // neutral look than AddTrackButton so the primary "add a track" action
    // still stands out as the main one.
    namespace SecondaryButton
    {
        inline const juce::Colour backgroundColour { 0xff26262b };
        inline const juce::Colour textColour       { juce::Colours::white };
    }

    //==============================================================================
    // Transport controls: Play / Pause / Stop buttons and the time readout
    namespace Transport
    {
        constexpr int buttonSizePx = 80;
        constexpr int rowHeightPx  = 90; // height reserved for the whole transport row

        inline const juce::Colour playColour       { 0xff2fbf5f }; // default (not playing)
        inline const juce::Colour playOverColour   { 0xff3fe673 };
        inline const juce::Colour playDownColour   { 0xff239048 };
        inline const juce::Colour playActiveColour { 0xff3fe673 }; // fill used while playback is active, in place of a border

        inline const juce::Colour pauseColour     { 0xffdf9a1f };
        inline const juce::Colour pauseOverColour { 0xffffb62e };
        inline const juce::Colour pauseDownColour { 0xffa8720f };

        inline const juce::Colour stopColour     { 0xffd0392f };
        inline const juce::Colour stopOverColour { 0xffef4f42 };
        inline const juce::Colour stopDownColour { 0xff9c281f };

        inline const juce::Colour timeTextColour { juce::Colours::white };
        constexpr float timeFontSizePx = 56.0f;
    }

    //==============================================================================
    // Help text line at the bottom of the window
    namespace HelpText
    {
        inline const juce::Colour textColour { juce::Colours::lightgrey };
        constexpr float fontSizePx = 13.0f;
    }

    //==============================================================================
    // Version number in the bottom bar
    namespace VersionLabel
    {
        inline const juce::Colour textColour { juce::Colours::grey };
        constexpr float fontSizePx = 11.0f;
    }
}

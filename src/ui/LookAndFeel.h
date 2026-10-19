#pragma once

#include <jdm/ui/LookAndFeel.h>

// Meridian's face of the shared family design language (jdm_shared): the
// common near-black blue-grey surfaces with Meridian's amber control accent.
// Cyan is the reserved hue - gain reduction only - so GR activity still pops
// against the amber accent; level metering stays green.

namespace mrdn
{

inline const jdm::colors::Palette meridianPalette {
    juce::Colour (0xff15181e), juce::Colour (0xff1d2129),
    juce::Colour (0xff242a34), juce::Colour (0xff313845),
    juce::Colour (0xffd8dce4), juce::Colour (0xff828a9a),
    juce::Colour (0xffe09a44), juce::Colour (0xff86642a),
    juce::Colour (0xff46b5dc), juce::Colour (0xff56b58c),
    juce::Colour (0xffd96a5a), juce::Colour (0xffb287d8)
};

inline const jdm::colors::Palette meridianPaletteHighContrast {
    juce::Colour (0xff090b0e), juce::Colour (0xff11141a),
    juce::Colour (0xff1b2029), juce::Colour (0xff5a6478),
    juce::Colour (0xfff4f7fc), juce::Colour (0xffbcc5d6),
    juce::Colour (0xffffb45c), juce::Colour (0xff9f7435),
    juce::Colour (0xff62cdf4), juce::Colour (0xff6bd9a6),
    juce::Colour (0xffff8071), juce::Colour (0xffcfa5f4)
};

namespace colors
{
    using namespace jdm::colors;

    // Meridian's reserved hue means gain reduction (cyan).
    inline juce::Colour& gainRed = jdm::colors::reserved;
}

struct LookAndFeel : jdm::LookAndFeel
{
    LookAndFeel() : jdm::LookAndFeel (meridianPalette, meridianPaletteHighContrast) {}
};

} // namespace mrdn

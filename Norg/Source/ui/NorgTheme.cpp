#include "NorgTheme.h"

#include <BinaryData.h>

namespace norg::ui
{
    namespace
    {
        juce::Typeface::Ptr load (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
        }

        const juce::Typeface::Ptr& logoFace()
        {
            static const auto face = load (NorgBinary::ArchivoBlackRegular_ttf, NorgBinary::ArchivoBlackRegular_ttfSize);
            return face;
        }

        const juce::Typeface::Ptr& labelFace()
        {
            static const auto face = load (NorgBinary::BarlowSemiCondensedSemiBold_ttf,
                                           NorgBinary::BarlowSemiCondensedSemiBold_ttfSize);
            return face;
        }

        const juce::Typeface::Ptr& boldFace()
        {
            static const auto face = load (NorgBinary::BarlowSemiCondensedBold_ttf,
                                           NorgBinary::BarlowSemiCondensedBold_ttfSize);
            return face;
        }
    }

    juce::Font Fonts::logo (float height)      { return juce::Font (juce::FontOptions (logoFace()).withHeight (height)); }
    juce::Font Fonts::label (float height)     { return juce::Font (juce::FontOptions (labelFace()).withHeight (height)); }
    juce::Font Fonts::labelBold (float height) { return juce::Font (juce::FontOptions (boldFace()).withHeight (height)); }
    juce::Font Fonts::display (float height)   { return juce::Font (juce::FontOptions (labelFace()).withHeight (height)); }

    void drawSilkscreen (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                         juce::Justification justification, float height, bool bold, juce::Colour colour)
    {
        auto font = bold ? Fonts::labelBold (height) : Fonts::label (height);
        font.setExtraKerningFactor (0.06f);
        g.setFont (font);
        g.setColour (colour);
        g.drawText (text.toUpperCase(), area, justification, false);
    }
}

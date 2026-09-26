#include "Widgets.h"

namespace norg::ui
{
    //==============================================================================
    Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::String label,
                bool bipolar)
        : labelText (std::move (label))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        slider.setPopupDisplayEnabled (true, true, nullptr, 1200);
        slider.getProperties().set ("bipolar", bipolar);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

        if (auto* param = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    void Knob::resized()
    {
        auto area = getLocalBounds();
        area.removeFromBottom (16);
        slider.setBounds (area);
    }

    void Knob::paint (juce::Graphics& g)
    {
        drawSilkscreen (g, labelText, getLocalBounds().removeFromBottom (15).toFloat(),
                        juce::Justification::centred, 11.5f);
    }

    //==============================================================================
    LedButton::LedButton (juce::String label, juce::Colour ledColour)
        : juce::Button (label), labelText (std::move (label)), led (ledColour)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void LedButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto bounds = getLocalBounds().toFloat();
        const bool lit = ledOverride.value_or (getToggleState());

        // Layout: LED on top, key cap in the middle, label below.
        const float labelHeight = labelText.isEmpty() ? 0.0f : 14.0f;
        auto area = bounds;
        const auto labelArea = area.removeFromBottom (labelHeight);
        const auto ledArea = area.removeFromTop (10.0f);
        auto cap = area.reduced (2.0f, 1.5f);
        cap = cap.withSizeKeepingCentre (juce::jmin (cap.getWidth(), 40.0f), juce::jmin (cap.getHeight(), 18.0f));

        NorgLookAndFeel::drawLed (g, ledArea.getCentre(), 3.0f, lit, led);

        const float press = down ? 1.0f : 0.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 3.0f);
        juce::ColourGradient capFill (highlighted ? colours::keyCapHi : colours::keyCap.brighter (0.08f),
                                      cap.getX(), cap.getY() + press,
                                      colours::keyCap.darker (0.35f), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (capFill);
        g.fillRoundedRectangle (cap.translated (0.0f, press), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (cap.translated (0.0f, press).reduced (0.5f), 3.0f, 1.0f);

        if (labelHeight > 0.0f)
            drawSilkscreen (g, labelText, labelArea, juce::Justification::centred, 11.0f);
    }

    //==============================================================================
    ParamLedButton::ParamLedButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                                    juce::String label)
        : button (std::move (label))
    {
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
    }

    //==============================================================================
    ChoiceButtons::ChoiceButtons (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                                  juce::StringArray labels)
    {
        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);

        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = buttons.add (new LedButton (labels[i]));
            b->onClick = [this, i]
            {
                if (attachment != nullptr)
                    attachment->setValueAsCompleteGesture (static_cast<float> (i));
            };
            addAndMakeVisible (b);
        }

        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { update (v); });
        attachment->sendInitialUpdate();
    }

    void ChoiceButtons::update (float value)
    {
        const int index = juce::roundToInt (value);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    void ChoiceButtons::resized()
    {
        if (buttons.isEmpty())
            return;

        auto area = getLocalBounds();
        const int w = area.getWidth() / buttons.size();
        for (auto* b : buttons)
            b->setBounds (area.removeFromLeft (w));
    }

    //==============================================================================
    void Oled::setText (juce::String title, juce::String subtitle, juce::String footer)
    {
        if (title == titleText && subtitle == subtitleText && footer == footerText)
            return;
        titleText = std::move (title);
        subtitleText = std::move (subtitle);
        footerText = std::move (footer);
        repaint();
    }

    void Oled::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Bezel
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 5.0f);
        g.setColour (juce::Colour (0xff111214));
        g.fillRoundedRectangle (bounds, 5.0f);

        auto screen = bounds.reduced (5.0f);
        g.setColour (colours::oledBack);
        g.fillRoundedRectangle (screen, 2.5f);

        auto text = screen.reduced (8.0f, 5.0f);
        g.setColour (colours::oledDim);
        g.setFont (Fonts::display (12.0f));
        g.drawText (subtitleText, text.removeFromTop (14.0f), juce::Justification::centredLeft, true);

        g.setColour (colours::oledText);
        g.setFont (Fonts::labelBold (24.0f));
        g.drawFittedText (titleText, text.removeFromTop (text.getHeight() - (footerText.isEmpty() ? 0.0f : 14.0f)).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);

        if (footerText.isNotEmpty())
        {
            g.setColour (colours::oledDim);
            g.setFont (Fonts::display (12.0f));
            g.drawText (footerText, text, juce::Justification::centredLeft, true);
        }

        // Faint scanline sheen
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (screen.withHeight (screen.getHeight() * 0.45f));
    }

    //==============================================================================
    SectionFrame::SectionFrame (juce::String title) : titleText (std::move (title))
    {
        setInterceptsMouseClicks (false, true);
    }

    juce::Rectangle<int> SectionFrame::content() const
    {
        return getLocalBounds().reduced (10).withTrimmedTop (22);
    }

    void SectionFrame::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        // Slightly darker inset panel with a thin silkscreen outline.
        g.setColour (colours::panelRedDeep.withAlpha (0.35f));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (colours::silkscreen.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds, 6.0f, 1.2f);

        // Title plate: white block with red lettering.
        auto titleFont = Fonts::labelBold (15.0f);
        titleFont.setExtraKerningFactor (0.12f);
        const float titleWidth = juce::GlyphArrangement::getStringWidth (titleFont, titleText.toUpperCase()) + 18.0f;
        const auto plate = juce::Rectangle<float> (bounds.getX() + 10.0f, bounds.getY() - 1.0f, titleWidth, 19.0f);
        g.setColour (colours::silkscreen);
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (colours::panelRed);
        g.setFont (titleFont);
        g.drawText (titleText.toUpperCase(), plate, juce::Justification::centred, false);

        if (noteText.isNotEmpty())
            drawSilkscreen (g, noteText, bounds.removeFromBottom (22.0f).reduced (10.0f, 0.0f),
                            juce::Justification::centredRight, 10.5f, false, colours::silkscreenDim);
    }

    //==============================================================================
    void NorgLogo::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().toFloat();

        const auto wordmark = Fonts::logo (area.getHeight() * 0.86f);
        g.setFont (wordmark);
        const float wordWidth = juce::GlyphArrangement::getStringWidth (wordmark, "norg");
        const auto wordArea = area.removeFromLeft (wordWidth + 4.0f);

        // Soft shadow, then the white wordmark.
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawText ("norg", wordArea.translated (1.5f, 2.0f), juce::Justification::centredLeft, false);
        g.setColour (colours::silkscreen);
        g.drawText ("norg", wordArea, juce::Justification::centredLeft, false);

        // Model badge sits on the baseline, like "stage" after the maker's name.
        auto badgeFont = Fonts::labelBold (area.getHeight() * 0.5f);
        badgeFont.setExtraKerningFactor (0.02f);
        g.setFont (badgeFont);
        g.setColour (colours::silkscreen);
        g.drawText (modelName.toLowerCase(), area.withTrimmedLeft (6.0f).withTrimmedBottom (area.getHeight() * 0.12f),
                    juce::Justification::bottomLeft, false);
    }
}

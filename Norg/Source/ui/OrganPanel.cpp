#include "OrganPanel.h"
#include "params/Parameters.h"

namespace norg::ui
{
    namespace
    {
        // Drawbar handle colours: 0 brown (sub-harmonics), 1 white (octaves), 2 black (other harmonics).
        const std::array<std::array<int, 9>, 4> modelColours { {
            { 0, 0, 1, 1, 2, 1, 2, 2, 1 },  // B3
            { 1, 1, 1, 1, 2, 2, 2, 2, 0 },  // Vox: flutes, reeds, brightness
            { 0, 1, 1, 2, 2, 1, 1, 1, 2 },  // Farf tabs
            { 0, 1, 1, 1, 1, 2, 1, 2, 0 },  // Pipe stops
        } };

        const std::array<juce::StringArray, 4> modelLabels {
            juce::StringArray { "16'", "5 1/3", "8'", "4'", "2 2/3", "2'", "1 3/5", "1 1/3", "1'" },
            juce::StringArray { "16'", "8'", "4'", "IV", "16'", "8'", "4'", "IV", "Brt" },
            juce::StringArray { "B16", "S16", "F8", "O8", "T8", "S8", "F4", "S4", "2 2/3" },
            juce::StringArray { "Bdn16", "Prin8", "Fl8", "Oct4", "Fl4", "Naz", "Sup2", "Tier", "Mix" },
        };

        juce::Colour handleColour (int colourIndex)
        {
            switch (colourIndex)
            {
                case 0:  return juce::Colour (0xff7a4a2a);
                case 2:  return juce::Colour (0xff1c1c1e);
                default: return juce::Colour (0xfff2eee4);
            }
        }
    }

    //==============================================================================
    Drawbar::Drawbar (juce::AudioProcessorValueTreeState& s, DrawbarStyle st, int i)
        : state (s), style (st), index (i)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void Drawbar::bind (const juce::String& paramId)
    {
        attachment.reset();
        value = 0;

        if (auto* param = paramId.isNotEmpty() ? state.getParameter (paramId) : nullptr)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
            {
                value = juce::roundToInt (v);
                repaint();
            });
            attachment->sendInitialUpdate();
        }

        setEnabled (attachment != nullptr);
        repaint();
    }

    void Drawbar::setValue (int v, bool asGesture)
    {
        v = juce::jlimit (0, 8, v);
        if (attachment == nullptr || v == value)
            return;

        if (asGesture)
            attachment->setValueAsPartOfGesture (static_cast<float> (v));
        else
            attachment->setValueAsCompleteGesture (static_cast<float> (v));
    }

    juce::Rectangle<float> Drawbar::travelArea() const
    {
        return getLocalBounds().toFloat().reduced (1.0f, 0.0f);
    }

    int Drawbar::valueFromLedY (float y) const
    {
        const float h = static_cast<float> (getHeight());
        return juce::jlimit (1, 8, 1 + static_cast<int> (y / h * 8.0f));
    }

    void Drawbar::mouseDown (const juce::MouseEvent& e)
    {
        if (attachment == nullptr)
            return;

        dragStartValue = value;
        attachment->beginGesture();

        if (style == DrawbarStyle::leds)
        {
            // Clicking the top lit LED turns it off; clicking any other LED sets the level there.
            const int clicked = valueFromLedY (e.position.y);
            setValue (clicked == value ? clicked - 1 : clicked, true);
            dragStartValue = value;
        }
    }

    void Drawbar::mouseDrag (const juce::MouseEvent& e)
    {
        if (attachment == nullptr)
            return;

        const float stepPixels = static_cast<float> (getHeight()) / 9.0f;
        setValue (dragStartValue + juce::roundToInt (static_cast<float> (e.getDistanceFromDragStartY()) / stepPixels), true);
    }

    void Drawbar::mouseUp (const juce::MouseEvent&)
    {
        if (attachment != nullptr)
            attachment->endGesture();
    }

    void Drawbar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        if (std::abs (wheel.deltaY) > 0.0f)
            setValue (value + (wheel.deltaY < 0.0f ? 1 : -1), false);
    }

    void Drawbar::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        const bool enabled = attachment != nullptr;

        if (style == DrawbarStyle::leds)
        {
            // Eight LEDs, lit from the top down to the drawbar's setting.
            const float cell = bounds.getHeight() / 8.0f;
            for (int k = 1; k <= 8; ++k)
            {
                const auto centre = juce::Point<float> (bounds.getCentreX(), bounds.getY() + cell * (static_cast<float> (k) - 0.5f));
                const auto led = juce::Rectangle<float> (bounds.getWidth() * 0.55f, cell * 0.42f).withCentre (centre);
                const bool lit = enabled && k <= value;
                if (lit)
                {
                    g.setColour (colours::ledRed.withAlpha (0.25f));
                    g.fillRoundedRectangle (led.expanded (2.5f), 3.0f);
                }
                g.setColour (lit ? colours::ledRed : colours::ledOff.withAlpha (enabled ? 1.0f : 0.4f));
                g.fillRoundedRectangle (led, 1.5f);
            }
            return;
        }

        // Sliding drawbar: a dark slot, the shaft that has been pulled out, and the handle.
        const float handleHeight = 24.0f;
        const float travel = bounds.getHeight() - handleHeight;
        const float handleY = travel * static_cast<float> (value) / 8.0f;
        const float cx = bounds.getCentreX();

        g.setColour (juce::Colour (0xff0b0b0c));
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 7.0f, 0.0f, 14.0f, travel + 4.0f), 3.0f);

        const auto base = handleColour (colourIndex).withMultipliedAlpha (enabled ? 1.0f : 0.35f);
        const auto shaft = juce::Rectangle<float> (cx - 5.0f, 0.0f, 10.0f, handleY + 2.0f);
        g.setColour (base.darker (0.25f));
        g.fillRect (shaft);

        g.setFont (Fonts::labelBold (9.0f));
        g.setColour (colourIndex == 1 ? juce::Colours::black.withAlpha (0.6f) : juce::Colours::white.withAlpha (0.7f));
        for (int k = 1; k <= value; ++k)
            g.drawText (juce::String (k), juce::Rectangle<float> (cx - 5.0f, (static_cast<float> (k) - 1.0f) * travel / 8.0f, 10.0f, travel / 8.0f),
                        juce::Justification::centred, false);

        const auto handle = juce::Rectangle<float> (bounds.getX() + 1.0f, handleY, bounds.getWidth() - 2.0f, handleHeight);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (handle.translated (0.0f, 2.0f), 4.0f);
        juce::ColourGradient grad (base.brighter (0.25f), handle.getX(), handle.getY(), base.darker (0.2f), handle.getX(), handle.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (handle, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (colourIndex == 1 ? 0.6f : 0.18f));
        g.drawHorizontalLine (static_cast<int> (handle.getY() + 2.0f), handle.getX() + 3.0f, handle.getRight() - 3.0f);
    }

    //==============================================================================
    //==============================================================================
    OrganPanel::OrganPanel (juce::AudioProcessorValueTreeState& s, DrawbarStyle st)
        : SectionFrame ("Organ"),
          state (s), style (st),
          onButton (s, paramId (P::organOn), "On"),
          volume (s, paramId (P::organVolume), "Volume"),
          click (s, paramId (P::organClick), "Click"),
          models (s, paramId (P::organModel), { "B3", "Vox", "Farf", "Pipe" }),
          presets (s, paramId (P::organPreset), { "Preset I", "Preset II" }),
          percOn (s, paramId (P::organPercOn), "Perc"),
          percThird (s, paramId (P::organPercThird), "3rd"),
          percFast (s, paramId (P::organPercFast), "Fast"),
          percSoft (s, paramId (P::organPercSoft), "Soft"),
          vibOn (s, paramId (P::organVibOn), "Vib/Ch"),
          vibMode (s, paramId (P::organVibMode), "Mode"),
          splitButton (s, paramId (P::organSplit), "Split"),
          pedalsButton (s, paramId (P::organPedals), "Pedals")
    {
        for (int i = 0; i < 9; ++i)
            addAndMakeVisible (drawbars.add (new Drawbar (state, style, i)));

        for (auto* c : std::initializer_list<juce::Component*> { &onButton, &volume, &click, &models, &presets,
                                                                 &percOn, &percThird, &percFast, &percSoft, &vibOn,
                                                                 &vibMode, &splitButton, &pedalsButton,
                                                                 &upperButton, &lowerButton, &pedalButton })
            addAndMakeVisible (c);

        upperButton.onClick = [this] { setTarget (Target::upper); };
        lowerButton.onClick = [this] { setTarget (Target::lower); };
        pedalButton.onClick = [this] { setTarget (Target::pedal); };

        const auto watch = [this] (P p, std::function<void (float)> fn)
        {
            auto a = std::make_unique<juce::ParameterAttachment> (*state.getParameter (paramId (p)), std::move (fn));
            a->sendInitialUpdate();
            return a;
        };

        modelWatch = watch (P::organModel, [this] (float v) { model = juce::roundToInt (v); rebind(); });
        presetWatch = watch (P::organPreset, [this] (float v) { preset = juce::roundToInt (v); rebind(); });
        splitWatch = watch (P::organSplit, [this] (float v)
        {
            split = v >= 0.5f;
            if (! split)
                target = Target::upper;
            rebind();
        });
    }

    OrganPanel::~OrganPanel() = default;

    void OrganPanel::setTarget (Target t)
    {
        target = split ? t : Target::upper;
        rebind();
    }

    void OrganPanel::rebind()
    {
        for (int i = 0; i < 9; ++i)
        {
            juce::String id;
            if (target == Target::upper)
                id = "organ_db" + juce::String (preset + 1) + "_" + juce::String (i + 1);
            else if (target == Target::lower)
                id = "organ_lower_" + juce::String (i + 1);
            else if (i < 2)
                id = i == 0 ? "organ_pedal_16" : "organ_pedal_8";

            drawbars[i]->bind (id);
            drawbars[i]->setColourIndex (target == Target::pedal ? 0 : modelColours[static_cast<size_t> (model)][static_cast<size_t> (i)]);
        }

        footageLabels = target == Target::pedal ? juce::StringArray { "16'", "8'" } : modelLabels[static_cast<size_t> (model)];

        upperButton.setToggleState (target == Target::upper, juce::dontSendNotification);
        lowerButton.setToggleState (target == Target::lower, juce::dontSendNotification);
        pedalButton.setToggleState (target == Target::pedal, juce::dontSendNotification);
        lowerButton.setEnabled (split);
        pedalButton.setEnabled (split);

        // Percussion only exists on the tonewheel model.
        for (auto* b : { &percOn, &percThird, &percFast, &percSoft })
            b->setEnabled (model == 0);

        repaint();
    }

    void OrganPanel::resized()
    {
        auto area = content();

        auto left = area.removeFromLeft (76);
        onButton.setBounds (left.removeFromTop (50).withSizeKeepingCentre (56, 50));
        left.removeFromTop (6);
        volume.setBounds (left.removeFromTop (92));
        left.removeFromTop (6);
        click.setBounds (left.removeFromTop (78).withSizeKeepingCentre (64, 78));
        area.removeFromLeft (10);

        auto top = area.removeFromTop (46);
        models.setBounds (top.removeFromLeft (4 * 52));
        presets.setBounds (top.removeFromRight (2 * 58));
        area.removeFromTop (6);

        auto right = area.removeFromRight (128);
        area.removeFromRight (8);
        const int rowHeight = right.getHeight() / 4;
        const auto place = [&right, rowHeight] (juce::Component& a, juce::Component& b)
        {
            auto row = right.removeFromTop (rowHeight);
            a.setBounds (row.removeFromLeft (64).withSizeKeepingCentre (60, juce::jmin (rowHeight, 48)));
            b.setBounds (row.withSizeKeepingCentre (60, juce::jmin (rowHeight, 48)));
        };
        place (percOn, percThird);
        place (percFast, percSoft);
        place (vibOn, vibMode);
        place (splitButton, pedalsButton);

        auto bottom = area.removeFromBottom (48);
        const int buttonWidth = bottom.getWidth() / 3;
        upperButton.setBounds (bottom.removeFromLeft (buttonWidth).withSizeKeepingCentre (60, 48));
        lowerButton.setBounds (bottom.removeFromLeft (buttonWidth).withSizeKeepingCentre (60, 48));
        pedalButton.setBounds (bottom.withSizeKeepingCentre (60, 48));

        area.removeFromBottom (16); // footage labels
        const int barWidth = area.getWidth() / 9;
        for (auto* d : drawbars)
            d->setBounds (area.removeFromLeft (barWidth).reduced (style == DrawbarStyle::sliding ? 2 : 1, 0));
    }

    void OrganPanel::paint (juce::Graphics& g)
    {
        SectionFrame::paint (g);

        for (int i = 0; i < drawbars.size(); ++i)
        {
            const auto bar = drawbars[i]->getBounds();
            const auto label = juce::Rectangle<int> (bar.getX() - 4, bar.getBottom() + 2, bar.getWidth() + 8, 14).toFloat();
            drawSilkscreen (g, footageLabels[i], label, juce::Justification::centred, 10.0f, false,
                            drawbars[i]->isEnabled() ? colours::silkscreen : colours::silkscreenDim);
        }
    }
}

#include "Rows.h"

namespace
{
    constexpr int pad = 14;
    constexpr int buttonHeight = 30;

    bool isButtonRow (Row::Style s) { return s == Row::Style::primary || s == Row::Style::quiet; }

    int textHeight (const juce::String& text, const juce::Font& font, int width)
    {
        if (text.isEmpty() || width <= 0)
            return 0;
        juce::AttributedString a;
        a.append (text, font);
        a.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (a, (float) width);
        return (int) std::ceil (layout.getHeight());
    }
}

void RowComponent::set (const Row& r)
{
    row = r;
    juce::StringArray labels;
    for (const auto& a : r.actions)
        labels.add (a.label);
    if (labels != buttonLabels)
    {
        buttons.clear();
        buttonLabels = labels;
        for (size_t i = 0; i < r.actions.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (r.actions[i].label));
            if (r.actions[i].primary)
                b->setColour (juce::TextButton::buttonColourId, theme::accent);
            addAndMakeVisible (b);
        }
    }
    for (size_t i = 0; i < r.actions.size() && i < (size_t) buttons.size(); ++i)
    {
        auto fn = r.actions[i].fn;
        buttons[(int) i]->onClick = [fn] { if (fn) fn(); };
    }
    resized();
    repaint();
}

int RowComponent::preferredHeight (int width) const
{
    if (isButtonRow (row.style))
        return buttonHeight + 8;
    const int textWidth = width - 2 * pad - (row.style == Row::Style::member ? 24 : 0)
                          - (row.actions.empty() || row.style != Row::Style::member ? 0 : 130);
    const auto titleFont = theme::font (row.style == Row::Style::heading ? 13.0f : 15.0f, row.style != Row::Style::text);
    int h = textHeight (row.text, titleFont, textWidth);
    h += textHeight (row.detail, theme::font (13.0f), textWidth) + (row.detail.isNotEmpty() ? 3 : 0);
    if (row.style == Row::Style::heading)
        return h + 18;
    if (row.style == Row::Style::text)
        return h + 6;
    if (! row.actions.empty() && row.style != Row::Style::member)
        h += buttonHeight + 10;
    if (row.progress >= 0.0f)
        h += 16;
    return h + 2 * 11;
}

juce::Rectangle<int> RowComponent::textArea() const
{
    auto r = getLocalBounds().reduced (pad, row.style == Row::Style::text ? 2 : 11);
    if (row.style == Row::Style::heading)
        r = getLocalBounds().reduced (2, 0).withTrimmedTop (12);
    if (row.style == Row::Style::member)
    {
        r.removeFromLeft (24);
        if (! row.actions.empty())
            r.removeFromRight (130);
    }
    else if (! row.actions.empty())
        r.removeFromBottom (buttonHeight + 10);
    return r;
}

void RowComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    switch (row.style)
    {
        case Row::Style::card:
        case Row::Style::member:
            g.setColour (theme::card);
            g.fillRoundedRectangle (bounds.reduced (0.0f, 3.0f), 10.0f);
            break;
        case Row::Style::warning:
            g.setColour (theme::warn.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds.reduced (0.0f, 3.0f), 10.0f);
            g.setColour (theme::warn.withAlpha (0.6f));
            g.drawRoundedRectangle (bounds.reduced (0.5f, 3.5f), 10.0f, 1.0f);
            break;
        case Row::Style::good:
            g.setColour (theme::card);
            g.fillRoundedRectangle (bounds.reduced (0.0f, 3.0f), 10.0f);
            g.setColour (theme::good);
            g.fillEllipse (bounds.getX() + pad - 2.0f, bounds.getCentreY() - 4.0f, 8.0f, 8.0f);
            break;
        default:
            break;
    }

    if (isButtonRow (row.style))
        return;

    auto area = textArea();
    if (row.style == Row::Style::good)
        area.removeFromLeft (14);
    if (row.style == Row::Style::member)
    {
        auto dotArea = juce::Rectangle<float> ((float) pad, (float) getHeight() / 2.0f - 7.0f, 14.0f, 14.0f);
        g.setColour (row.dim ? row.dot.withAlpha (0.35f) : row.dot);
        g.fillEllipse (dotArea);
    }

    const auto titleColour = row.style == Row::Style::heading ? theme::dim : (row.dim ? theme::faint : theme::text);
    const auto titleFont = theme::font (row.style == Row::Style::heading ? 13.0f : 15.0f, row.style != Row::Style::text);
    juce::AttributedString a;
    a.setWordWrap (juce::AttributedString::byWord);
    a.append (row.style == Row::Style::heading ? row.text.toUpperCase() : row.text, titleFont,
              row.style == Row::Style::text ? theme::dim : titleColour);
    if (row.detail.isNotEmpty())
        a.append ("\n" + row.detail, theme::font (13.0f), row.dim ? theme::faint : theme::dim);
    juce::TextLayout layout;
    layout.createLayout (a, (float) area.getWidth());
    const float y = row.style == Row::Style::member ? (float) area.getY() + juce::jmax (0.0f, (area.getHeight() - layout.getHeight()) / 2.0f)
                                                    : (float) area.getY();
    layout.draw (g, juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), layout.getHeight()));

    if (row.progress >= 0.0f)
    {
        juce::Rectangle<float> bar ((float) area.getX(), y + layout.getHeight() + 7.0f, (float) area.getWidth(), 6.0f);
        g.setColour (theme::cardHi);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (row.progressBad ? theme::bad : theme::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, row.progress)), 3.0f);
    }

    if (row.crown && layout.getNumLines() > 0)
    {
        // A small "Player 1" badge after the name, on its first line.
        const auto& line = layout.getLine (0);
        const float nameWidth = line.getLineBoundsX().getEnd();
        auto badgeFont = theme::font (11.0f, true);
        const float w = juce::GlyphArrangement::getStringWidth (badgeFont, "PLAYER 1") + 12.0f;
        juce::Rectangle<float> badge ((float) area.getX() + nameWidth + 8.0f, y + line.ascent - 13.0f, w, 17.0f);
        g.setColour (theme::warn.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (theme::warn);
        g.setFont (badgeFont);
        g.drawText ("PLAYER 1", badge, juce::Justification::centred);
    }
}

void RowComponent::resized()
{
    if (buttons.isEmpty())
        return;
    if (isButtonRow (row.style))
    {
        auto r = getLocalBounds().reduced (0, 4);
        const int w = r.getWidth() / buttons.size();
        for (auto* b : buttons)
            b->setBounds (r.removeFromLeft (w).reduced (buttons.size() > 1 ? 3 : 0, 0));
        return;
    }
    if (row.style == Row::Style::member)
    {
        auto r = getLocalBounds().reduced (pad, 0).removeFromRight (124).withSizeKeepingCentre (124, 28);
        for (auto* b : buttons)
            b->setBounds (r);
        return;
    }
    auto r = getLocalBounds().reduced (pad, 11).removeFromBottom (buttonHeight);
    for (int i = buttons.size(); --i >= 0;)
    {
        auto* b = buttons[i];
        const int w = juce::jmax (90, juce::GlyphArrangement::getStringWidthInt (theme::font (14.0f, true), b->getButtonText()) + 28);
        b->setBounds (r.removeFromRight (w));
        r.removeFromRight (8);
    }
}

void RowList::setRows (const std::vector<Row>& rows)
{
    std::map<std::string, std::unique_ptr<RowComponent>> next;
    order.clear();
    for (const auto& r : rows)
    {
        auto it = components.find (r.id);
        std::unique_ptr<RowComponent> c;
        if (it != components.end())
        {
            c = std::move (it->second);
            components.erase (it);
        }
        else
        {
            c = std::make_unique<RowComponent>();
            addAndMakeVisible (*c);
        }
        c->set (r);
        order.push_back (r.id);
        next[r.id] = std::move (c);
    }
    components = std::move (next); // anything not in `rows` is deleted here
    layout (getWidth());
}

void RowList::layout (int width)
{
    int y = 0;
    for (const auto& id : order)
    {
        auto& c = components[id];
        const int h = c->preferredHeight (width);
        c->setBounds (0, y, width, h);
        y += h + 4;
    }
    setSize (width, y + 8);
}

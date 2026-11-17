#pragma once

#include "Theme.h"

#include <functional>
#include <map>

// The main window is mostly a vertical list of rows: headings, cards with buttons, people.
// Rows are matched by id between updates, so buttons keep their state and nothing flickers.
struct Row
{
    enum class Style { heading, text, card, warning, good, member, primary, quiet };

    struct Action
    {
        juce::String label;
        std::function<void()> fn;
        bool primary = false;
    };

    std::string id;
    Style style = Style::card;
    juce::String text, detail;
    juce::Colour dot { 0 };   // member colour (member rows)
    bool crown = false;       // Player 1
    bool dim = false;         // offline
    std::vector<Action> actions;
};

class RowComponent : public juce::Component
{
public:
    void set (const Row&);
    int preferredHeight (int width) const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> textArea() const;

    Row row;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::StringArray buttonLabels;
};

class RowList : public juce::Component
{
public:
    void setRows (const std::vector<Row>& rows);
    void layout (int width);

private:
    std::map<std::string, std::unique_ptr<RowComponent>> components;
    std::vector<std::string> order;
};

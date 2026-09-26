#include "PluginEditor.h"

namespace
{
const juce::Colour kBackground { 0xff15171c };
const juce::Colour kPanel { 0xff1e2129 };
const juce::Colour kText { 0xffe6e8ee };
const juce::Colour kDim { 0xff8a90a0 };
const juce::Colour kAccent { 0xff4f8cff };
const juce::Colour kError { 0xffff6b6b };

constexpr int kHeaderHeight = 48;
constexpr int kBannerHeight = 34;
constexpr int kFooterHeight = 36;

void styleSectionLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, kDim);
}
} // namespace

GeneratorLookAndFeel::GeneratorLookAndFeel()
{
    setColourScheme ({ kBackground, kPanel, kPanel, kDim, kText, kAccent, juce::Colours::white, kAccent, kText });
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e38));
    setColour (juce::TextButton::buttonOnColourId, kAccent);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2e38));
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2e38));
    setColour (juce::ListBox::backgroundColourId, kPanel);
    setColour (juce::Slider::thumbColourId, kAccent);
    setColour (juce::Slider::trackColourId, kAccent.withAlpha (0.6f));
    setColour (juce::ToggleButton::tickColourId, kAccent);
}

SerumPresetGeneratorEditor::SerumPresetGeneratorEditor (SerumPresetGeneratorProcessor& p)
    : AudioProcessorEditor (&p), proc_ (p), banner_ (p.updater)
{
    setLookAndFeel (&lnf_);

    title_.setText ("SERUM PRESET GENERATOR", juce::dontSendNotification);
    title_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title_.setColour (juce::Label::textColourId, kText);
    version_.setText ("Baastik Labs  v" + juce::String (SPG_VERSION_STRING), juce::dontSendNotification);
    version_.setColour (juce::Label::textColourId, kDim);
    for (auto* c : std::initializer_list<juce::Component*> { &title_, &version_, &settings_, &status_ })
        addAndMakeVisible (c);
    addChildComponent (banner_);
    banner_.onVisibilityChanged = [this] { resized(); };
    settings_.onClick = [this] { showSettingsMenu(); };

    // ----- left column
    styleSectionLabel (modeLabel_, "Mode");
    styleSectionLabel (categoryLabel_, "Sound type");
    styleSectionLabel (genreLabel_, "Genre");
    styleSectionLabel (chaosLabel_, "Chaos");
    styleSectionLabel (seedLabel_, "Seed");
    styleSectionLabel (batchLabel_, "How many");
    for (auto* c : std::initializer_list<juce::Component*> { &modeLabel_, &categoryLabel_, &genreLabel_, &chaosLabel_, &seedLabel_,
                                                             &batchLabel_, &random_, &guided_, &mutate_, &category_, &genre_,
                                                             &chaos_, &batch_, &seed_, &newSeed_, &generate_ })
        addAndMakeVisible (c);

    const std::pair<juce::TextButton*, serumgen::Mode> modes[] = {
        { &random_, serumgen::Mode::Random }, { &guided_, serumgen::Mode::Guided }, { &mutate_, serumgen::Mode::Mutate } };
    for (const auto& [button, mode] : modes)
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (1001);
        button->onClick = [this, mode = mode] {
            proc_.settings.mode = mode;
            updateModeUi();
        };
    }
    random_.setConnectedEdges (juce::Button::ConnectedOnRight);
    guided_.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    mutate_.setConnectedEdges (juce::Button::ConnectedOnLeft);
    random_.setTooltip ("Anything goes: every part is randomized across its full range.");
    guided_.setTooltip ("Follows the sound type and genre you pick. Chaos loosens the rules.");
    mutate_.setTooltip ("Makes variations of the selected preset. Chaos sets how far they drift.");

    category_.onChange = [this] {
        const auto i = category_.getSelectedId() - 1;
        if (i >= 0 && i < static_cast<int> (categoryIds_.size()))
            proc_.settings.category = categoryIds_[static_cast<std::size_t> (i)];
    };
    genre_.onChange = [this] {
        const auto i = genre_.getSelectedId() - 1;
        if (i >= 0 && i < static_cast<int> (genreIds_.size()))
            proc_.settings.genre = genreIds_[static_cast<std::size_t> (i)];
    };

    chaos_.setRange (0.0, 1.0, 0.01);
    chaos_.setTextValueSuffix ("");
    chaos_.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    chaos_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
    chaos_.onValueChange = [this] { proc_.settings.chaos = chaos_.getValue(); };
    batch_.setRange (1, 50, 1);
    batch_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
    batch_.onValueChange = [this] { proc_.batchCount = static_cast<int> (batch_.getValue()); };

    seed_.setInputRestrictions (10, "0123456789");
    seed_.onTextChange = [this] { proc_.settings.seed = static_cast<std::uint64_t> (seed_.getText().getLargeIntValue()); };
    newSeed_.onClick = [this] {
        proc_.randomizeSeed = newSeed_.getToggleState();
        seed_.setEnabled (! proc_.randomizeSeed);
    };

    generate_.setColour (juce::TextButton::buttonColourId, kAccent);
    generate_.onClick = [this] { generate(); };

    // ----- middle column
    styleSectionLabel (groupsLabel_, "Randomize");
    groupsHint_.setText ("Unticked parts are kept from the selected preset.", juce::dontSendNotification);
    groupsHint_.setColour (juce::Label::textColourId, kDim);
    groupsHint_.setFont (juce::FontOptions (12.0f));
    groupsHint_.setJustificationType (juce::Justification::topLeft);
    baseLabel_.setColour (juce::Label::textColourId, kDim);
    baseLabel_.setFont (juce::FontOptions (12.0f));
    baseLabel_.setJustificationType (juce::Justification::topLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &groupsLabel_, &groupsHint_, &baseLabel_, &allOn_, &allOff_, &loadBase_ })
        addAndMakeVisible (c);
    for (const auto g : serumgen::kAllGroups)
    {
        auto* t = groupToggles_.add (new juce::ToggleButton (juce::String (std::string (serumgen::groupLabel (g)))));
        t->onClick = [this, g, t] { proc_.settings.setLocked (g, ! t->getToggleState()); };
        addAndMakeVisible (t);
    }
    const auto setAll = [this] (bool on) {
        for (std::size_t i = 0; i < serumgen::kAllGroups.size(); ++i)
        {
            proc_.settings.setLocked (serumgen::kAllGroups[i], ! on);
            groupToggles_[static_cast<int> (i)]->setToggleState (on, juce::dontSendNotification);
        }
    };
    allOn_.onClick = [setAll] { setAll (true); };
    allOff_.onClick = [setAll] { setAll (false); };
    loadBase_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Load a Serum 2 preset", proc_.outputFolder, "*.SerumPreset");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const auto& fc) {
            if (fc.getResult().existsAsFile())
            {
                const auto error = proc_.loadBase (fc.getResult());
                setStatus (error.isEmpty() ? "Loaded " + fc.getResult().getFileName() : error, error.isNotEmpty());
            }
        });
    };

    // ----- right column
    styleSectionLabel (historyLabel_, "Presets");
    dragHint_.setText ("Drag a preset into Serum to load it.", juce::dontSendNotification);
    dragHint_.setColour (juce::Label::textColourId, kDim);
    dragHint_.setFont (juce::FontOptions (12.0f));
    history_.setRowHeight (40);
    history_.addMouseListener (this, true);
    summary_.setMultiLine (true);
    summary_.setReadOnly (true);
    summary_.setScrollbarsShown (true);
    summary_.setFont (juce::FontOptions (12.5f));
    for (auto* c : std::initializer_list<juce::Component*> { &historyLabel_, &dragHint_, &history_, &summary_, &reveal_, &delete_ })
        addAndMakeVisible (c);
    reveal_.onClick = [this] {
        if (const auto* e = proc_.selected())
            e->file.revealToUser();
    };
    delete_.onClick = [this] { deleteKeyPressed (history_.getSelectedRow()); };

    // ----- footer
    outputLabel_.setColour (juce::Label::textColourId, kDim);
    outputLabel_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (outputLabel_);
    addAndMakeVisible (changeOutput_);
    changeOutput_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Where should presets be saved?", proc_.outputFolder);
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const auto& fc) {
            if (fc.getResult() != juce::File())
            {
                proc_.outputFolder = fc.getResult();
                syncFromProcessor();
            }
        });
    };

    proc_.onHistoryChanged = [this] { refreshHistory(); };

    rebuildMenus();
    syncFromProcessor();
    refreshHistory();
    setSize (1000, 660);
    setResizable (true, true);
    setResizeLimits (860, 580, 1600, 1100);

    checkForUpdates (false);
}

SerumPresetGeneratorEditor::~SerumPresetGeneratorEditor()
{
    proc_.onHistoryChanged = nullptr;
    history_.removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void SerumPresetGeneratorEditor::rebuildMenus()
{
    const auto content = proc_.content.current();

    category_.clear (juce::dontSendNotification);
    categoryIds_ = { "any" };
    category_.addItem ("Any (let the genre decide)", 1);
    juce::String family;
    for (const auto& c : content->categories())
    {
        if (c.family != family.toStdString())
        {
            family = c.family;
            category_.addSectionHeading (c.family == "acoustic" ? "Acoustic & Instruments"
                                         : c.family == "drums"  ? "Drums"
                                         : c.family == "fx"     ? "FX"
                                                                : "Synth");
        }
        categoryIds_.push_back (c.id);
        category_.addItem (c.name, static_cast<int> (categoryIds_.size()));
    }

    genre_.clear (juce::dontSendNotification);
    genreIds_ = { "none", "any" };
    genre_.addItem ("None", 1);
    genre_.addItem ("Surprise me", 2);
    family.clear();
    for (const auto& g : content->genres())
    {
        if (g.family != family.toStdString())
        {
            family = g.family;
            genre_.addSectionHeading (g.family == "acoustic" ? "Acoustic & Band" : "Electronic");
        }
        genreIds_.push_back (g.id);
        genre_.addItem (g.name, static_cast<int> (genreIds_.size()));
    }
}

void SerumPresetGeneratorEditor::syncFromProcessor()
{
    const auto& s = proc_.settings;
    random_.setToggleState (s.mode == serumgen::Mode::Random, juce::dontSendNotification);
    guided_.setToggleState (s.mode == serumgen::Mode::Guided, juce::dontSendNotification);
    mutate_.setToggleState (s.mode == serumgen::Mode::Mutate, juce::dontSendNotification);

    const auto indexOf = [] (const std::vector<std::string>& ids, const std::string& id) {
        const auto it = std::find (ids.begin(), ids.end(), id);
        return it == ids.end() ? 1 : static_cast<int> (it - ids.begin()) + 1;
    };
    category_.setSelectedId (indexOf (categoryIds_, s.category), juce::dontSendNotification);
    genre_.setSelectedId (indexOf (genreIds_, s.genre), juce::dontSendNotification);
    chaos_.setValue (s.chaos, juce::dontSendNotification);
    batch_.setValue (proc_.batchCount, juce::dontSendNotification);
    seed_.setText (juce::String (s.seed), juce::dontSendNotification);
    newSeed_.setToggleState (proc_.randomizeSeed, juce::dontSendNotification);
    seed_.setEnabled (! proc_.randomizeSeed);
    for (std::size_t i = 0; i < serumgen::kAllGroups.size(); ++i)
        groupToggles_[static_cast<int> (i)]->setToggleState (! s.isLocked (serumgen::kAllGroups[i]), juce::dontSendNotification);
    outputLabel_.setText ("Saving to: " + proc_.outputFolder.getFullPathName(), juce::dontSendNotification);
    updateModeUi();
}

void SerumPresetGeneratorEditor::updateModeUi()
{
    const auto mode = proc_.settings.mode;
    category_.setEnabled (mode == serumgen::Mode::Guided);
    genre_.setEnabled (mode == serumgen::Mode::Guided);
    generate_.setButtonText (mode == serumgen::Mode::Mutate ? "Mutate" : "Generate");
    groupsLabel_.setText (mode == serumgen::Mode::Mutate ? "VARY" : "RANDOMIZE", juce::dontSendNotification);
    chaosLabel_.setText (mode == serumgen::Mode::Mutate ? "AMOUNT" : "CHAOS", juce::dontSendNotification);
}

void SerumPresetGeneratorEditor::refreshHistory()
{
    history_.updateContent();
    const int selected = proc_.selectedIndex();
    if (selected >= 0)
        history_.selectRow (selected, false, true);
    else
        history_.deselectAllRows();

    const auto* e = proc_.selected();
    summary_.setText (e != nullptr ? e->name + "\n" + e->description + "\n\n" + e->summary.joinIntoString ("\n") : juce::String(),
                      juce::dontSendNotification);
    baseLabel_.setText (e != nullptr ? "Base: " + e->name : "Base: Serum init patch (select a preset to build on it)",
                        juce::dontSendNotification);
    history_.repaint();
}

void SerumPresetGeneratorEditor::generate()
{
    const auto error = proc_.generate();
    if (error.isNotEmpty())
    {
        setStatus (error, true);
        return;
    }
    seed_.setText (juce::String (proc_.settings.seed), juce::dontSendNotification);
    const auto count = std::max (1, proc_.batchCount);
    setStatus (count == 1 ? "Saved " + proc_.history().front().name : "Saved " + juce::String (count) + " presets");
}

void SerumPresetGeneratorEditor::setStatus (const juce::String& text, bool error)
{
    status_.setText (text, juce::dontSendNotification);
    status_.setColour (juce::Label::textColourId, error ? kError : kDim);
}

void SerumPresetGeneratorEditor::checkForUpdates (bool userInitiated)
{
    juce::Component::SafePointer<SerumPresetGeneratorEditor> safe (this);
    auto onResult = [safe, userInitiated] (std::optional<baastik::ReleaseInfo> update, juce::var manifest) {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        self.proc_.content.updateFromManifest (manifest, [safe] {
            if (safe == nullptr)
                return;
            safe->rebuildMenus();
            safe->syncFromProcessor();
            safe->setStatus ("New sounds and genres downloaded (content v" + juce::String (safe->proc_.content.version()) + ")");
        });
        if (update)
            self.banner_.showUpdate (*update);
        else if (userInitiated)
            self.banner_.showMessage (manifest.isObject() ? "You're on the latest version." : "Couldn't reach the update server.");
    };
    if (userInitiated)
        proc_.updater.checkNow (onResult);
    else
        proc_.updater.checkIfDue (onResult);
}

void SerumPresetGeneratorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Check for updates now", [this] { checkForUpdates (true); });
    menu.addItem ("Check for updates automatically", true, proc_.updater.isAutoCheckEnabled(),
                  [this] { proc_.updater.setAutoCheckEnabled (! proc_.updater.isAutoCheckEnabled()); });
    menu.addSeparator();
    menu.addItem ("Open output folder", [this] {
        proc_.outputFolder.createDirectory();
        proc_.outputFolder.revealToUser();
    });
    menu.addItem ("Reset output folder to default", [this] {
        proc_.outputFolder = SerumPresetGeneratorProcessor::defaultOutputFolder();
        syncFromProcessor();
    });
    menu.addSeparator();
    menu.addItem (juce::String ("Sound content v") + juce::String (proc_.content.version())
                      + (proc_.content.isDownloaded() ? " (downloaded)" : " (built in)"),
                  false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settings_));
}

// ------------------------------------------------------------------ history

int SerumPresetGeneratorEditor::getNumRows()
{
    return static_cast<int> (proc_.history().size());
}

void SerumPresetGeneratorEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows())
        return;
    const auto& e = proc_.history()[static_cast<std::size_t> (row)];
    if (selected)
        g.fillAll (kAccent.withAlpha (0.25f));
    g.setColour (kText);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (e.name, 10, 3, width - 20, height / 2, juce::Justification::bottomLeft, true);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (e.description, 10, height / 2 + 1, width - 20, height / 2 - 4, juce::Justification::topLeft, true);
}

void SerumPresetGeneratorEditor::selectedRowsChanged (int lastRowSelected)
{
    if (lastRowSelected != proc_.selectedIndex())
    {
        proc_.select (lastRowSelected);
        refreshHistory();
    }
}

void SerumPresetGeneratorEditor::deleteKeyPressed (int row)
{
    if (row < 0 || row >= getNumRows())
        return;
    const auto name = proc_.history()[static_cast<std::size_t> (row)].name;
    juce::Component::SafePointer<SerumPresetGeneratorEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset", "Delete \"" + name + "\" from disk?",
                                        "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe, row] (int result) {
                                            if (safe != nullptr && result == 1)
                                                safe->proc_.removeFromHistory (row, true);
                                        }));
}

void SerumPresetGeneratorEditor::mouseDrag (const juce::MouseEvent& e)
{
    // Dragging a history row out of the plugin hands the file to the OS, so
    // it can be dropped straight into Serum.
    if (! history_.isParentOf (e.eventComponent) || e.getDistanceFromDragStart() < 8 || isDragAndDropActive())
        return;
    const auto pos = e.getEventRelativeTo (&history_).getPosition();
    const int row = history_.getRowContainingPosition (pos.x, pos.y);
    if (row < 0 || row >= getNumRows())
        return;
    const auto file = proc_.history()[static_cast<std::size_t> (row)].file;
    if (file.existsAsFile())
        performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

// ------------------------------------------------------------------- layout

void SerumPresetGeneratorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    auto area = getLocalBounds();
    area.removeFromTop (kHeaderHeight + (banner_.isVisible() ? kBannerHeight : 0));
    area.removeFromBottom (kFooterHeight);
    area.reduce (12, 8);
    g.setColour (kPanel);
    const int left = 290, middle = 230;
    g.fillRoundedRectangle (area.removeFromLeft (left).toFloat(), 8.0f);
    area.removeFromLeft (10);
    g.fillRoundedRectangle (area.removeFromLeft (middle).toFloat(), 8.0f);
    area.removeFromLeft (10);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
}

void SerumPresetGeneratorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (14, 8);
    settings_.setBounds (header.removeFromRight (90).reduced (0, 2));
    title_.setBounds (header.removeFromLeft (280));
    version_.setBounds (header);
    if (banner_.isVisible())
        banner_.setBounds (area.removeFromTop (kBannerHeight));

    auto footer = area.removeFromBottom (kFooterHeight).reduced (14, 6);
    changeOutput_.setBounds (footer.removeFromRight (90));
    footer.removeFromRight (8);
    status_.setBounds (footer.removeFromRight (footer.getWidth() / 3));
    outputLabel_.setBounds (footer);

    area.reduce (12, 8);
    const auto row = [] (juce::Rectangle<int>& r, int h) {
        auto out = r.removeFromTop (h);
        r.removeFromTop (6);
        return out;
    };

    // Left column.
    auto left = area.removeFromLeft (290).reduced (14, 12);
    area.removeFromLeft (10);
    modeLabel_.setBounds (row (left, 16));
    auto modes = row (left, 30);
    const int third = modes.getWidth() / 3;
    random_.setBounds (modes.removeFromLeft (third));
    guided_.setBounds (modes.removeFromLeft (third));
    mutate_.setBounds (modes);
    left.removeFromTop (6);
    categoryLabel_.setBounds (row (left, 16));
    category_.setBounds (row (left, 28));
    genreLabel_.setBounds (row (left, 16));
    genre_.setBounds (row (left, 28));
    chaosLabel_.setBounds (row (left, 16));
    chaos_.setBounds (row (left, 26));
    seedLabel_.setBounds (row (left, 16));
    auto seedRow = row (left, 26);
    seed_.setBounds (seedRow.removeFromLeft (110));
    seedRow.removeFromLeft (8);
    newSeed_.setBounds (seedRow);
    batchLabel_.setBounds (row (left, 16));
    batch_.setBounds (row (left, 26));
    generate_.setBounds (left.removeFromBottom (48));

    // Middle column.
    auto middle = area.removeFromLeft (230).reduced (14, 12);
    area.removeFromLeft (10);
    groupsLabel_.setBounds (row (middle, 16));
    groupsHint_.setBounds (row (middle, 30));
    for (auto* t : groupToggles_)
        t->setBounds (row (middle, 24));
    auto allRow = row (middle, 26);
    allOn_.setBounds (allRow.removeFromLeft (allRow.getWidth() / 2 - 3));
    allRow.removeFromLeft (6);
    allOff_.setBounds (allRow);
    middle.removeFromTop (6);
    loadBase_.setBounds (middle.removeFromBottom (28));
    middle.removeFromBottom (4);
    baseLabel_.setBounds (middle.removeFromBottom (34));

    // Right column.
    auto right = area.reduced (14, 12);
    historyLabel_.setBounds (row (right, 16));
    auto buttons = right.removeFromBottom (28);
    delete_.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (6);
    reveal_.setBounds (buttons.removeFromRight (90));
    dragHint_.setBounds (buttons);
    right.removeFromBottom (6);
    summary_.setBounds (right.removeFromBottom (juce::jmax (140, right.getHeight() / 2 - 20)));
    right.removeFromBottom (6);
    history_.setBounds (right);
}

namespace baastik
{
UpdateBanner::UpdateBanner (UpdateChecker& checker)
    : checker_ (checker)
{
    text_.setColour (juce::Label::textColourId, juce::Colours::white);
    text_.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (text_);
    for (auto* b : { &install_, &later_, &skip_ })
        addAndMakeVisible (*b);
    addChildComponent (bar_);

    install_.onClick = [this] { startInstall(); };
    later_.onClick = [this] { setShown (false); };
    skip_.onClick = [this] {
        if (release_)
            checker_.skipVersion (release_->version);
        setShown (false);
    };
    setVisible (false);
}

void UpdateBanner::showUpdate (const ReleaseInfo& release)
{
    release_ = release;
    text_.setText (checker_.config().displayName + " " + release.version.toString() + " is available"
                       + (release.notes.isNotEmpty() ? ": " + release.notes.upToFirstOccurrenceOf ("\n", false, false) : juce::String()),
                   juce::dontSendNotification);
    install_.setVisible (true);
    skip_.setVisible (true);
    later_.setButtonText ("Later");
    bar_.setVisible (false);
    setShown (true);
}

void UpdateBanner::showMessage (const juce::String& message)
{
    text_.setText (message, juce::dontSendNotification);
    install_.setVisible (false);
    skip_.setVisible (false);
    later_.setButtonText ("Close");
    bar_.setVisible (false);
    setShown (true);
}

void UpdateBanner::startInstall()
{
    if (! release_)
        return;
    progress_ = -1.0; // indeterminate until the size is known
    bar_.setVisible (true);
    install_.setVisible (false);
    skip_.setVisible (false);
    text_.setText ("Downloading " + release_->version.toString() + "...", juce::dontSendNotification);
    resized();

    juce::Component::SafePointer<UpdateBanner> safe (this);
    checker_.downloadAndInstall (
        *release_,
        [safe] (float p) {
            if (safe != nullptr)
                safe->progress_ = p;
        },
        [safe] (bool, juce::String message) {
            if (safe != nullptr)
                safe->showMessage (message);
        });
}

void UpdateBanner::setShown (bool shown)
{
    setVisible (shown);
    if (onVisibilityChanged)
        onVisibilityChanged();
}

void UpdateBanner::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2d6cdf));
}

void UpdateBanner::resized()
{
    auto r = getLocalBounds().reduced (8, 5);
    for (auto* b : { &later_, &skip_, &install_ })
        if (b->isVisible())
        {
            b->setBounds (r.removeFromRight (b == &skip_ ? 100 : 90));
            r.removeFromRight (6);
        }
    if (bar_.isVisible())
        bar_.setBounds (r.removeFromRight (160).reduced (0, 3));
    text_.setBounds (r);
}
} // namespace baastik

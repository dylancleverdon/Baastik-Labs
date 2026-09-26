// Shared test entry point: brings up JUCE's message manager (parameters and timers need it)
// before running Catch2.
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

#include <cstdlib>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Tests never touch the user's program library. They get an empty one, so a new processor
    // starts on the Init sound (every parameter at its default).
    const juce::TemporaryFile programs (".norglib");
    programs.getFile().replaceWithText ("<NorgPrograms version=\"1\"><Live/></NorgPrograms>");
    setenv ("NORG_PROGRAM_LIBRARY", programs.getFile().getFullPathName().toRawUTF8(), 1);

    return Catch::Session().run (argc, argv);
}

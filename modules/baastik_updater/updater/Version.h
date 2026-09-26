#pragma once

namespace baastik
{
// "major.minor.patch" with missing parts treated as 0.
struct Version
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    static Version parse (const juce::String& text)
    {
        auto parts = juce::StringArray::fromTokens (text.trimCharactersAtStart ("vV"), ".", {});
        Version v;
        v.major = parts.size() > 0 ? parts[0].getIntValue() : 0;
        v.minor = parts.size() > 1 ? parts[1].getIntValue() : 0;
        v.patch = parts.size() > 2 ? parts[2].getIntValue() : 0;
        return v;
    }

    auto tie() const { return std::tie (major, minor, patch); }
    bool operator< (const Version& other) const { return tie() < other.tie(); }
    bool operator== (const Version& other) const { return tie() == other.tie(); }

    juce::String toString() const { return juce::String (major) + "." + juce::String (minor) + "." + juce::String (patch); }
};
} // namespace baastik

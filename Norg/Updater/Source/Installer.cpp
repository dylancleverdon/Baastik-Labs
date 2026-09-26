#include "Installer.h"

#include <filesystem>

namespace norg::update
{
    bool FileOps::move (const juce::File& from, const juce::File& to)
    {
        if (! from.exists() || to.exists())
            return false;

        std::error_code ec;
        std::filesystem::rename (from.getFullPathName().toStdString(), to.getFullPathName().toStdString(), ec);
        return ! ec;
    }

    bool FileOps::removeRecursively (const juce::File& target)
    {
        if (! target.exists())
            return true;

        std::error_code ec;
        std::filesystem::remove_all (target.getFullPathName().toStdString(), ec);
        return ! ec;
    }

    bool FileOps::copyFile (const juce::File& from, const juce::File& to)
    {
        return from.copyFileTo (to);
    }

    juce::Result swapIn (const Layout& layout, const juce::File& payloadDir, FileOps& ops)
    {
        struct Step
        {
            InstallItem item;
            bool movedOld = false;
            bool placedNew = false;
        };

        const auto incoming = layout.supportDir().getChildFile ("previous.incoming");
        ops.removeRecursively (incoming);
        if (! incoming.createDirectory())
            return juce::Result::fail ("could not create " + incoming.getFullPathName());

        std::vector<Step> done;

        const auto undo = [&]
        {
            for (auto it = done.rbegin(); it != done.rend(); ++it)
            {
                if (it->placedNew)
                    ops.move (it->item.destination, payloadDir.getChildFile (it->item.name));
                if (it->movedOld)
                    ops.move (incoming.getChildFile (it->item.name), it->item.destination);
            }
            ops.removeRecursively (incoming);
        };

        for (const auto& item : layout.items())
        {
            const auto source = payloadDir.getChildFile (item.name);
            if (! source.exists())
                continue;

            item.destination.getParentDirectory().createDirectory();
            done.push_back ({ item });

            if (item.destination.exists())
            {
                if (! ops.move (item.destination, incoming.getChildFile (item.name)))
                {
                    done.pop_back();
                    undo();
                    return juce::Result::fail ("could not move aside " + item.destination.getFullPathName());
                }
                done.back().movedOld = true;
            }

            if (! ops.move (source, item.destination))
            {
                undo();
                return juce::Result::fail ("could not install " + item.destination.getFullPathName());
            }
            done.back().placedNew = true;
        }

        if (done.empty())
        {
            ops.removeRecursively (incoming);
            return juce::Result::fail ("the update contained nothing to install");
        }

        if (layout.installedJson().existsAsFile())
            ops.copyFile (layout.installedJson(), incoming.getChildFile ("installed.json"));

        const auto release = payloadDir.getChildFile ("release.json");
        if (release.existsAsFile())
            ops.copyFile (release, layout.installedJson());

        ops.removeRecursively (layout.previousDir());
        ops.move (incoming, layout.previousDir());
        return juce::Result::ok();
    }

    bool hasPrevious (const Layout& layout)
    {
        for (const auto& item : layout.items())
            if (layout.previousDir().getChildFile (item.name).exists())
                return true;

        return false;
    }

    juce::Result rollback (const Layout& layout, FileOps& ops)
    {
        if (! hasPrevious (layout))
            return juce::Result::fail ("there is no previous version to roll back to");

        const auto previous = layout.previousDir();
        const auto holding = layout.supportDir().getChildFile ("rollback.tmp");
        ops.removeRecursively (holding);
        if (! holding.createDirectory())
            return juce::Result::fail ("could not create " + holding.getFullPathName());

        struct Swapped
        {
            InstallItem item;
            bool hadCurrent = false;
        };
        std::vector<Swapped> swapped;

        const auto undo = [&]
        {
            for (auto it = swapped.rbegin(); it != swapped.rend(); ++it)
            {
                ops.move (it->item.destination, previous.getChildFile (it->item.name));
                if (it->hadCurrent)
                    ops.move (holding.getChildFile (it->item.name), it->item.destination);
            }
            ops.removeRecursively (holding);
        };

        for (const auto& item : layout.items())
        {
            const auto old = previous.getChildFile (item.name);
            if (! old.exists())
                continue;

            const bool hadCurrent = item.destination.exists();
            if (hadCurrent && ! ops.move (item.destination, holding.getChildFile (item.name)))
            {
                undo();
                return juce::Result::fail ("could not move aside " + item.destination.getFullPathName());
            }

            item.destination.getParentDirectory().createDirectory();
            if (! ops.move (old, item.destination))
            {
                if (hadCurrent)
                    ops.move (holding.getChildFile (item.name), item.destination);
                undo();
                return juce::Result::fail ("could not restore " + item.destination.getFullPathName());
            }

            swapped.push_back ({ item, hadCurrent });
        }

        // What was current becomes "previous", so a second rollback rolls forward again.
        for (const auto& s : swapped)
            if (s.hadCurrent)
                ops.move (holding.getChildFile (s.item.name), previous.getChildFile (s.item.name));

        const auto currentJson = layout.installedJson();
        const auto previousJson = previous.getChildFile ("installed.json");
        const auto heldJson = holding.getChildFile ("installed.json");
        if (currentJson.existsAsFile())  ops.move (currentJson, heldJson);
        if (previousJson.existsAsFile()) ops.move (previousJson, currentJson);
        if (heldJson.existsAsFile())     ops.move (heldJson, previousJson);

        ops.removeRecursively (holding);
        return juce::Result::ok();
    }
}

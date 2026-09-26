#!/usr/bin/env python3
"""Writes a plugin's update.json for its release channel.

The plugin reads this file from
  https://github.com/<repo>/releases/download/<plugin>-latest/update.json
to find new installers and new generator content.

Full release:
  make_manifest.py --plugin serum-preset-generator --repo owner/repo \
      --version 0.1.12 --tag serum-preset-generator-v0.1.12 \
      --mac SerumPresetGenerator-0.1.12-macOS.pkg --windows SerumPresetGenerator-0.1.12-Windows.exe \
      --content-version 12 --commit <sha> --notes "..." > update.json

Content-only release (keeps the previous installers and version):
  make_manifest.py --plugin ... --repo ... --previous old-update.json \
      --content-version 13 --commit <sha> > update.json
"""

import argparse
import json
import sys
from datetime import datetime, timezone


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--plugin", required=True)
    p.add_argument("--repo", required=True)
    p.add_argument("--version")
    p.add_argument("--tag")
    p.add_argument("--mac")
    p.add_argument("--windows")
    p.add_argument("--content-version", type=int, required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--notes", default="")
    p.add_argument("--previous", help="previous update.json for content-only releases")
    a = p.parse_args()

    base = f"https://github.com/{a.repo}/releases/download"
    channel = f"{a.plugin}-latest"

    if a.previous:
        with open(a.previous) as f:
            manifest = json.load(f)
        if not manifest.get("version"):
            sys.exit("previous manifest has no version; a full release is needed first")
    else:
        if not (a.version and a.tag and a.mac and a.windows):
            sys.exit("--version, --tag, --mac and --windows are required for a full release")
        manifest = {
            "plugin": a.plugin,
            "version": a.version,
            "notes": a.notes,
            "downloads": {
                "mac": f"{base}/{a.tag}/{a.mac}",
                "windows": f"{base}/{a.tag}/{a.windows}",
            },
        }

    manifest["plugin"] = a.plugin
    manifest["commit"] = a.commit
    manifest["published"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    manifest["content"] = {
        "version": a.content_version,
        "url": f"{base}/{channel}/content.json",
    }
    json.dump(manifest, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()

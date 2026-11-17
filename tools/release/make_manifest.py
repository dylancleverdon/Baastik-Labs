#!/usr/bin/env python3
"""Writes tutti-update.json, the file every installed Tutti reads (hourly) from the tutti-channel
release to find updates and the suggestion box endpoint.

  make_manifest.py --version 0.1.42 --build 42 --commit <sha> --notes-file notes.md \
      --protocol-header core/include/tutti/Protocol.h \
      --asset mac=out/Tutti-0.1.42-mac.zip=https://github.com/.../tutti-v0.1.42/Tutti-0.1.42-mac.zip \
      --asset windows=... --suggestions suggestions/endpoint.txt > tutti-update.json
"""
import argparse
import hashlib
import json
import os
import re
import sys
from datetime import datetime, timezone


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--version", required=True)
    p.add_argument("--build", type=int, required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--notes-file")
    p.add_argument("--protocol-header", default="core/include/tutti/Protocol.h")
    p.add_argument("--asset", action="append", default=[], help="platform=path=url")
    p.add_argument("--suggestions", help="file holding the suggestion box endpoint URL")
    a = p.parse_args()

    notes = ""
    if a.notes_file and os.path.exists(a.notes_file):
        notes = open(a.notes_file, encoding="utf-8").read().strip()

    protocol = 0
    m = re.search(r"kProtocolVersion\s*=\s*(\d+)", open(a.protocol_header, encoding="utf-8").read())
    if m:
        protocol = int(m.group(1))

    platforms = {}
    for spec in a.asset:
        platform, path, url = spec.split("=", 2)
        platforms[platform] = {"name": os.path.basename(path), "url": url, "sha256": sha256(path),
                               "size": os.path.getsize(path)}
    if not platforms:
        sys.exit("no assets")

    manifest = {
        "schema": 1,
        "version": a.version,
        "build": a.build,
        "commit": a.commit,
        "notes": notes,
        "protocol": protocol,
        "published": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "platforms": platforms,
    }
    if a.suggestions and os.path.exists(a.suggestions):
        endpoint = open(a.suggestions, encoding="utf-8").read().strip()
        if endpoint.startswith("https://"):
            manifest["suggestions"] = {"endpoint": endpoint}
    json.dump(manifest, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()

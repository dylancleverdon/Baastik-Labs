#!/usr/bin/env python3
"""Release helpers for Cymatica's CI (adapted from Norg's norg_release.py).

  release.py manifest --version V --build B --commit C --notes-file F --zip Z --zip-url U --out cymatica-update.json
  release.py notes --since-tag-prefix cymatica-v --out notes.md
  release.py sha256 FILE
"""
import argparse
import datetime
import hashlib
import json
import subprocess
import sys


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read().strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout.strip()


def cmd_manifest(a):
    data = {
        "schema": 1,
        "version": a.version,
        "build": int(a.build),
        "commit": a.commit,
        "notes": read(a.notes_file),
        "zipName": a.zip.split("/")[-1],
        "zipUrl": a.zip_url,
        "sha256": sha256(a.zip),
        "publishedAt": now(),
    }
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def cmd_notes(a):
    tags = [t for t in git("tag", "--list", a.since_tag_prefix + "*", "--sort=-creatordate").splitlines() if t]
    rng = [f"{tags[0]}..HEAD"] if tags else ["-n", "15"]
    log = git("log", "--no-merges", "--pretty=format:- %s", *rng)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(log if log else "- Maintenance update")
        f.write("\n")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("manifest")
    for flag in ("--version", "--build", "--commit", "--notes-file", "--zip", "--zip-url", "--out"):
        m.add_argument(flag, required=True)
    n = sub.add_parser("notes")
    n.add_argument("--since-tag-prefix", default="cymatica-v")
    n.add_argument("--out", required=True)
    h = sub.add_parser("sha256")
    h.add_argument("file")
    a = p.parse_args()
    if a.cmd == "manifest":
        cmd_manifest(a)
    elif a.cmd == "notes":
        cmd_notes(a)
    else:
        print(sha256(a.file))


if __name__ == "__main__":
    sys.exit(main())

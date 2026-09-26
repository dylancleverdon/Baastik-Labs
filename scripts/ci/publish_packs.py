#!/usr/bin/env python3
"""Publishes Norg's sample packs to the norg-samples release (CI only).

Each pack in installer/sample-packs.json is downloaded from its source once, repackaged as
<id>-v<version>.tar.gz with the pack's folder name at the top, and uploaded with a .sha256
sidecar. packs.json (what installed Norgs read) is then rebuilt from the release's contents.
Packs already on the release are left alone, so this is quick after the first run.

  GH_TOKEN=... GH_REPO=owner/repo publish_packs.py [--target <sha>]
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

RELEASE = "norg-samples"


def gh(*args, capture=True):
    return subprocess.run(["gh", *args], check=True, capture_output=capture, text=True).stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def release_assets():
    try:
        out = gh("release", "view", RELEASE, "--json", "assets")
    except subprocess.CalledProcessError:
        return None
    return {a["name"]: a for a in json.loads(out)["assets"]}


def build_pack(pack, work):
    archive = os.path.join(work, "source.tar.gz")
    print(f"Downloading {pack['name']} from {pack['source']}", flush=True)
    subprocess.run(["curl", "-fL", "--retry", "3", "-o", archive, pack["source"]], check=True)

    unpacked = os.path.join(work, "unpacked")
    os.makedirs(unpacked)
    subprocess.run(["tar", "-xzf", archive, "-C", unpacked], check=True)
    os.remove(archive)

    tops = os.listdir(unpacked)
    root = os.path.join(unpacked, tops[0]) if len(tops) == 1 else unpacked
    staged = os.path.join(work, "staged")
    os.makedirs(staged)
    folder = os.path.join(staged, pack["folder"])
    shutil.move(root, folder)
    for junk in (".git", ".github", ".gitattributes", ".gitignore"):
        path = os.path.join(folder, junk)
        if os.path.isdir(path):
            shutil.rmtree(path)
        elif os.path.exists(path):
            os.remove(path)

    name = f"{pack['id']}-v{pack['version']}.tar.gz"
    out = os.path.join(work, name)
    # FLAC doesn't compress further, so a light gzip keeps this fast.
    with open(out, "wb") as f:
        tar = subprocess.Popen(["tar", "-cf", "-", "-C", staged, pack["folder"]], stdout=subprocess.PIPE)
        subprocess.run(["gzip", "-1"], stdin=tar.stdout, stdout=f, check=True)
        tar.wait()
        if tar.returncode != 0:
            sys.exit("tar failed")

    digest = sha256(out)
    with open(out + ".sha256", "w") as f:
        f.write(digest + "\n")
    return out, digest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default=None)
    ap.add_argument("--definitions", default="installer/sample-packs.json")
    a = ap.parse_args()

    repo = os.environ["GH_REPO"]
    with open(a.definitions) as f:
        packs = json.load(f)["packs"]

    assets = release_assets()
    if assets is None:
        args = ["release", "create", RELEASE, "--prerelease", "--latest=false", "--title", "Norg sample packs",
                "--notes", "Sample libraries that Norg downloads and installs by itself. Licences are listed in packs.json."]
        if a.target:
            args += ["--target", a.target]
        gh(*args)
        assets = {}

    entries = []
    for pack in packs:
        name = f"{pack['id']}-v{pack['version']}.tar.gz"
        with tempfile.TemporaryDirectory() as work:
            if name in assets and name + ".sha256" in assets:
                gh("release", "download", RELEASE, "--pattern", name + ".sha256", "--dir", work)
                with open(os.path.join(work, name + ".sha256")) as f:
                    digest = f.read().strip()
                size = assets[name]["size"]
                print(f"{name} already published", flush=True)
            else:
                path, digest = build_pack(pack, work)
                size = os.path.getsize(path)
                print(f"Uploading {name} ({size // (1 << 20)} MB)", flush=True)
                gh("release", "upload", RELEASE, path, path + ".sha256", "--clobber", capture=False)

        entries.append({
            "id": pack["id"], "name": pack["name"], "version": pack["version"], "folder": pack["folder"],
            "url": f"https://github.com/{repo}/releases/download/{RELEASE}/{name}",
            "sha256": digest, "size": size, "license": pack["license"],
        })

    with tempfile.TemporaryDirectory() as work:
        path = os.path.join(work, "packs.json")
        with open(path, "w") as f:
            json.dump({"packs": entries}, f, indent=2)
        gh("release", "upload", RELEASE, path, "--clobber", capture=False)
    print("packs.json published", flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build Pickless's offline US product pack from USDA FoodData Central branded foods.

USDA FoodData Central data is in the public domain (CC0). The pack is a gzipped SQLite file:

    products(gtin TEXT PRIMARY KEY,   -- 14-digit GTIN key, same as the app's Gtin.key()
             name, brand, ingredients, serving_g, serving_text, category, fat, sugar, fiber)
    products_fts (FTS4 over name, brand; docid = products.rowid)

Usage: build_pack.py --out out [--zip FoodData_Central_branded_food_csv_YYYY-MM-DD.zip] [--repo owner/name]
Without --zip it finds the newest branded CSV release on the FDC download page and downloads it.
"""
import argparse
import csv
import datetime
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import sqlite3
import sys
import urllib.request
import zipfile

DOWNLOAD_PAGE = "https://fdc.nal.usda.gov/download-datasets/"
FDC_HOST = "https://fdc.nal.usda.gov"
NUTRIENTS = {b"1004": "fat", b"2000": "sugar", b"1079": "fiber"}

csv.field_size_limit(10 * 1024 * 1024)


def latest_zip_url():
    html = urllib.request.urlopen(DOWNLOAD_PAGE, timeout=60).read().decode("utf-8", "replace")
    found = sorted(set(re.findall(r"/fdc-datasets/FoodData_Central_branded_food_csv_(\d{4}-\d{2}-\d{2})\.zip", html)))
    if not found:
        raise SystemExit("no branded CSV dataset link on the FDC download page")
    date = found[-1]
    return f"{FDC_HOST}/fdc-datasets/FoodData_Central_branded_food_csv_{date}.zip", date


def download(url, dest):
    with urllib.request.urlopen(url, timeout=120) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f, 1024 * 1024)


def check_digit_ok(code):
    if len(code) not in (8, 12, 13, 14) or not code.isdigit():
        return False
    body = code[:-1]
    total = sum(int(c) * (3 if i % 2 == 0 else 1) for i, c in enumerate(reversed(body)))
    return (10 - total % 10) % 10 == int(code[-1])


def gtin_key(raw):
    code = re.sub(r"\D", "", raw or "")
    if len(code.lstrip("0")) < 7:  # too short to be a real product barcode
        return None
    # FDC pads to 14 digits inconsistently; try the code as given, then without extra leading zeros.
    for candidate in (code, code.lstrip("0").rjust(12, "0"), code.lstrip("0").rjust(13, "0")):
        if len(candidate) in (8, 12, 13, 14) and check_digit_ok(candidate):
            return candidate.rjust(14, "0")
    return None


def member(z, name):
    for n in z.namelist():
        if n == name or n.endswith("/" + name):
            return n
    raise SystemExit(f"{name} missing from the zip")


def rows(z, name):
    with z.open(member(z, name)) as f:
        yield from csv.DictReader(io.TextIOWrapper(f, encoding="utf-8", newline=""))


def grams(size, unit):
    try:
        v = float(size)
    except (TypeError, ValueError):
        return None
    u = (unit or "").strip().lower()
    if u in ("g", "grm", "gram", "grams", "ml", "mlt", "milliliter", "milliliters"):
        return round(v, 1) if v > 0 else None
    if u in ("oz", "onz"):
        return round(v * 28.35, 1)
    return None


def build(zip_path, out_dir, version, repo):
    os.makedirs(out_dir, exist_ok=True)
    z = zipfile.ZipFile(zip_path)
    best = {}  # gtin -> (date, fdc_id, row subset)
    seen = 0
    for r in rows(z, "branded_food.csv"):
        seen += 1
        if (r.get("discontinued_date") or "").strip():
            continue
        country = (r.get("market_country") or "").strip()
        if country and country != "United States":
            continue
        key = gtin_key(r.get("gtin_upc"))
        if key is None:
            continue
        ingredients = (r.get("ingredients") or "").strip()
        if not ingredients:
            continue
        date = r.get("available_date") or r.get("modified_date") or ""
        fdc = int(r["fdc_id"])
        cur = best.get(key)
        if cur is None or (date, fdc) > (cur[0], cur[1]):
            best[key] = (date, fdc, (
                (r.get("brand_name") or r.get("brand_owner") or "").strip(),
                ingredients,
                grams(r.get("serving_size"), r.get("serving_size_unit")),
                (r.get("household_serving_fulltext") or "").strip(),
                (r.get("branded_food_category") or "").strip(),
            ))
    print(f"branded_food: {seen} rows, {len(best)} products kept", file=sys.stderr)
    by_fdc = {v[1]: k for k, v in best.items()}
    names = {}
    for r in rows(z, "food.csv"):
        fdc = int(r["fdc_id"]) if r.get("fdc_id", "").isdigit() else None
        if fdc in by_fdc:
            names[fdc] = (r.get("description") or "").strip()
    nutrients = {}
    with z.open(member(z, "food_nutrient.csv")) as f:
        f.readline()
        for line in f:
            parts = line.split(b'","', 4)
            if len(parts) < 4:
                continue
            what = NUTRIENTS.get(parts[2])
            if what is None:
                continue
            try:
                fdc = int(parts[1])
            except ValueError:
                continue
            if fdc not in by_fdc:
                continue
            try:
                nutrients.setdefault(fdc, {})[what] = float(parts[3])
            except ValueError:
                pass
    db_path = os.path.join(out_dir, "usda.db")
    if os.path.exists(db_path):
        os.remove(db_path)
    con = sqlite3.connect(db_path)
    con.execute("PRAGMA journal_mode=OFF")
    con.execute("CREATE TABLE products(gtin TEXT PRIMARY KEY, name TEXT, brand TEXT, ingredients TEXT, serving_g REAL, serving_text TEXT, category TEXT, fat REAL, sugar REAL, fiber REAL)")
    batch = []
    for key, (date, fdc, (brand, ingredients, serving_g, serving_text, category)) in best.items():
        name = names.get(fdc) or brand or "Product"
        n = nutrients.get(fdc, {})
        batch.append((key, name, brand, ingredients, serving_g, serving_text, category, n.get("fat"), n.get("sugar"), n.get("fiber")))
        if len(batch) >= 5000:
            con.executemany("INSERT OR REPLACE INTO products VALUES (?,?,?,?,?,?,?,?,?,?)", batch)
            batch.clear()
    con.executemany("INSERT OR REPLACE INTO products VALUES (?,?,?,?,?,?,?,?,?,?)", batch)
    con.execute('CREATE VIRTUAL TABLE products_fts USING fts4(content="products", name, brand)')
    con.execute("INSERT INTO products_fts(docid, name, brand) SELECT rowid, name, brand FROM products")
    con.execute("INSERT INTO products_fts(products_fts) VALUES('optimize')")
    count = con.execute("SELECT COUNT(*) FROM products").fetchone()[0]
    con.commit()
    con.execute("VACUUM")
    con.close()
    gz_name = f"usda-branded-{version}.db.gz"
    gz_path = os.path.join(out_dir, gz_name)
    with open(db_path, "rb") as src, gzip.open(gz_path, "wb", compresslevel=9) as dst:
        shutil.copyfileobj(src, dst, 1024 * 1024)
    os.remove(db_path)
    sha = hashlib.sha256()
    with open(gz_path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            sha.update(chunk)
    size = os.path.getsize(gz_path)
    pack = {
        "id": "usda-branded", "version": version, "built": datetime.date.today().isoformat(),
        "url": f"https://github.com/{repo}/releases/download/pickless-data/{gz_name}",
        "sha256": sha.hexdigest(), "size": size, "products": count,
        "note": "USDA FoodData Central branded foods (public domain)",
    }
    with open(os.path.join(out_dir, "packs.json"), "w") as f:
        json.dump({"packs": [pack]}, f, indent=2)
    print(f"pack: {count} products, {size / 1e6:.1f} MB -> {gz_path}", file=sys.stderr)
    return pack


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--zip")
    ap.add_argument("--version")
    ap.add_argument("--repo", default="dylancleverdon/Baastik-Labs")
    a = ap.parse_args(argv)
    zip_path, version = a.zip, a.version
    if not zip_path:
        url, version = latest_zip_url()
        os.makedirs(a.out, exist_ok=True)
        zip_path = os.path.join(a.out, "branded.zip")
        print(f"downloading {url}", file=sys.stderr)
        download(url, zip_path)
    if not version:
        m = re.search(r"(\d{4}-\d{2}-\d{2})", os.path.basename(zip_path))
        version = m.group(1) if m else datetime.date.today().isoformat()
    build(zip_path, a.out, version, a.repo)
    if not a.zip:
        os.remove(zip_path)


if __name__ == "__main__":
    main()

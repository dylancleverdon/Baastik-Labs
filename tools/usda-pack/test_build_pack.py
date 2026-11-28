import gzip
import io
import json
import os
import sqlite3
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
import build_pack  # noqa: E402


def make_zip(path):
    branded = (
        '"fdc_id","brand_owner","brand_name","subbrand_name","gtin_upc","ingredients","not_a_significant_source_of","serving_size","serving_size_unit",'
        '"household_serving_fulltext","branded_food_category","data_source","package_weight","modified_date","available_date","market_country","discontinued_date",'
        '"preparation_state_code","trade_channel","short_description","material_code"\n'
        '"1","Silk","Silk","","00025293600232","ALMONDMILK (FILTERED WATER, ALMONDS), CANE SUGAR","","240.0","ml","1 cup","Plant Based Milk","GDSN","","2020-01-01","2020-01-01","United States","","","","",""\n'
        '"2","Silk","Silk","","025293600232","ALMONDMILK (FILTERED WATER, ALMONDS), CANE SUGAR, SEA SALT","","240.0","ml","1 cup","Plant Based Milk","GDSN","","2024-01-01","2024-01-01","United States","","","","",""\n'
        '"3","Old","","","00012000161155","SUGAR","","10","g","","Candy","GDSN","","2020-01-01","2020-01-01","United States","2021-01-01","","","",""\n'
        '"4","Bad","","","123","WATER","","10","g","","Water","GDSN","","2020-01-01","2020-01-01","United States","","","","",""\n'
        '"5","Oatly","Oatly","","07394376616228","OAT BASE (WATER, OATS), RAPESEED OIL","","240","ml","1 cup","Plant Based Milk","GDSN","","2023-01-01","2023-01-01","United States","","","","",""\n'
    )
    food = ('"fdc_id","data_type","description","food_category_id","publication_date","market_country","trade_channel","microbe_data"\n'
            '"1","branded_food","Almondmilk Original","","2020","United States","",""\n'
            '"2","branded_food","Almondmilk Original Unsweetened","","2024","United States","",""\n'
            '"5","branded_food","Oatmilk The Original","","2023","United States","",""\n')
    nutrient = ('"id","fdc_id","nutrient_id","amount","data_points","derivation_id","min","max","median","footnote","min_year_acquired"\n'
                '"1","2","1004","1.04","","","","","","",""\n'
                '"2","2","2000","3.33","","","","","","",""\n'
                '"3","5","1004","1.5","","","","","","",""\n'
                '"4","2","1008","30","","","","","","",""\n')
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("FoodData_Central_branded_food_csv_2026-10-29/branded_food.csv", branded)
        z.writestr("FoodData_Central_branded_food_csv_2026-10-29/food.csv", food)
        z.writestr("FoodData_Central_branded_food_csv_2026-10-29/food_nutrient.csv", nutrient)


def test_builds_a_pack(tmp_path):
    zp = tmp_path / "FoodData_Central_branded_food_csv_2026-10-29.zip"
    make_zip(zp)
    out = tmp_path / "out"
    build_pack.main(["--out", str(out), "--zip", str(zp), "--repo", "me/repo"])
    packs = json.load(open(out / "packs.json"))["packs"]
    assert packs[0]["version"] == "2026-10-29" and packs[0]["products"] == 2
    assert packs[0]["url"] == "https://github.com/me/repo/releases/download/pickless-data/usda-branded-2026-10-29.db.gz"
    raw = gzip.decompress(open(out / "usda-branded-2026-10-29.db.gz", "rb").read())
    db = tmp_path / "x.db"
    db.write_bytes(raw)
    con = sqlite3.connect(db)
    row = con.execute("SELECT name, ingredients, serving_g, fat, sugar FROM products WHERE gtin = '00025293600232'").fetchone()
    assert row == ("Almondmilk Original Unsweetened", "ALMONDMILK (FILTERED WATER, ALMONDS), CANE SUGAR, SEA SALT", 240.0, 1.04, 3.33)
    hits = con.execute("SELECT p.name FROM products_fts f JOIN products p ON p.rowid = f.docid WHERE products_fts MATCH 'oatm*'").fetchall()
    assert hits == [("Oatmilk The Original",)]
    assert con.execute("SELECT COUNT(*) FROM products WHERE gtin = '00012000161155'").fetchone()[0] == 0  # discontinued


def test_gtin_key():
    assert build_pack.gtin_key("00025293600232") == "00025293600232"
    assert build_pack.gtin_key("025293600232") == "00025293600232"
    assert build_pack.gtin_key("123") is None

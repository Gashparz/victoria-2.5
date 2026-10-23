#!/usr/bin/env python3
"""Builds the single-file save editor: src/app.html with the scripts inlined.

    python build.py                     -> "Victoria2 Save Editor.html"
    python build.py --pack pack.js      -> also embeds a data pack (const BUILTIN_PACK = {...})
"""
import argparse, pathlib

here = pathlib.Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument("--pack", help="optional JS file defining BUILTIN_PACK (offline cultures/map of one mod)")
ap.add_argument("-o", "--out", default=str(here / "Victoria2 Save Editor.html"))
a = ap.parse_args()

src = here / "src"
html = (src / "app.html").read_text(encoding="utf-8")
parts = {"/*CORE*/": "core.js", "/*OPS*/": "ops.js", "/*MOD*/": "moddata.js", "/*MAP*/": "mapview.js"}
for marker, name in parts.items():
    html = html.replace(marker, (src / name).read_text(encoding="utf-8").replace("</script", "<\\/script"))
pack = pathlib.Path(a.pack).read_text(encoding="utf-8") if a.pack else ""
html = html.replace("/*PACK*/", pack)
pathlib.Path(a.out).write_text(html, encoding="utf-8")
print("wrote", a.out, len(html), "bytes")

#!/usr/bin/env python3
"""Stage the approved Vita Cover Studio artwork for the VPK build."""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
OUT = ROOT / "build-assets" / "sce_sys"

FILES = {
    ASSETS / "icon0.png": OUT / "icon0.png",
    ASSETS / "pic0.png": OUT / "pic0.png",
    ASSETS / "livearea" / "contents" / "bg0.png": OUT / "livearea" / "contents" / "bg0.png",
    ASSETS / "livearea" / "contents" / "startup.png": OUT / "livearea" / "contents" / "startup.png",
    ASSETS / "app_background.jpg": ROOT / "build-assets" / "data" / "app_background.jpg",
}

for source, destination in FILES.items():
    if not source.is_file():
        raise SystemExit(f"Required artwork is missing: {source.relative_to(ROOT)}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    print(f"{destination.relative_to(ROOT)} ({destination.stat().st_size} bytes)")

template = OUT / "livearea" / "contents" / "template.xml"
template.write_text(
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<livearea style="a1" format-ver="01.00" content-rev="5">\n'
    '  <livearea-background><image>bg0.png</image></livearea-background>\n'
    '  <gate><startup-image>startup.png</startup-image></gate>\n'
    '</livearea>\n',
    encoding="utf-8",
)
print(f"{template.relative_to(ROOT)} ({template.stat().st_size} bytes)")

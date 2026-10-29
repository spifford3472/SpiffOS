#!/usr/bin/env python3
"""Regenerate the boot bitmap from the unchanged original SVG using host librsvg.

Optional developer step: requires host librsvg, Cairo, DejaVu Sans and Pillow.
Ordinary builds use the stored RGB asset and Python standard library only.
"""
from pathlib import Path
import ctypes as C
import ctypes.util
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class Rectangle(C.Structure):
    """Native librsvg viewport structure, all values measured in pixels."""
    _fields_ = [(name, C.c_double) for name in ["x", "y", "width", "height"]]


def main():
    """Render original SVG, release native handles, save bitmap and provenance."""
    svg = ROOT / "assets/branding/spiffos-logo.svg"
    prefix = ROOT / "assets/branding/spiffos-boot-logo"
    rsvg = C.CDLL(ctypes.util.find_library("rsvg-2"))
    cairo = C.CDLL(ctypes.util.find_library("cairo"))
    gobject = C.CDLL(ctypes.util.find_library("gobject-2.0"))
    rsvg.rsvg_handle_new_from_data.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(C.c_void_p)]
    rsvg.rsvg_handle_new_from_data.restype = C.c_void_p
    rsvg.rsvg_handle_render_document.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(Rectangle), C.POINTER(C.c_void_p)]
    rsvg.rsvg_handle_render_document.restype = C.c_int
    cairo.cairo_image_surface_create.argtypes = [C.c_int, C.c_int, C.c_int]
    cairo.cairo_image_surface_create.restype = C.c_void_p
    cairo.cairo_create.argtypes = [C.c_void_p]
    cairo.cairo_create.restype = C.c_void_p
    cairo.cairo_set_source_rgb.argtypes = [C.c_void_p, C.c_double, C.c_double, C.c_double]
    cairo.cairo_set_source_rgb.restype = None
    cairo.cairo_paint.argtypes = [C.c_void_p]
    cairo.cairo_paint.restype = None
    cairo.cairo_surface_write_to_png.argtypes = [C.c_void_p, C.c_char_p]
    cairo.cairo_surface_write_to_png.restype = C.c_int
    cairo.cairo_destroy.argtypes = [C.c_void_p]
    cairo.cairo_destroy.restype = None
    cairo.cairo_surface_destroy.argtypes = [C.c_void_p]
    cairo.cairo_surface_destroy.restype = None
    gobject.g_object_unref.argtypes = [C.c_void_p]
    gobject.g_object_unref.restype = None
    error = C.c_void_p()
    # Owner-approved boot derivative: flatten only the backing rectangle so it
    # blends with the workspace. Keep the authoritative source SVG unchanged.
    document = ET.fromstring(svg.read_bytes())
    background = document.find("{http://www.w3.org/2000/svg}rect")
    if background is None or background.get("width") != "1000" or background.get("height") != "400":
        raise SystemExit("Original SVG background structure changed; review conversion")
    background.set("fill", "#091b35")
    background.set("rx", "0")
    boot_svg = ET.tostring(document)
    handle = rsvg.rsvg_handle_new_from_data(boot_svg, len(boot_svg), C.byref(error))
    if not handle:
        raise SystemExit("SVG load failed")
    surface = cairo.cairo_image_surface_create(0, 400, 160)  # Cairo ARGB32
    context = cairo.cairo_create(surface)
    try:
        # Composite rounded transparent corners onto the power-up background.
        cairo.cairo_set_source_rgb(context, 9/255, 27/255, 53/255)
        cairo.cairo_paint(context)
        viewport = Rectangle(0, 0, 400, 160)
        if not rsvg.rsvg_handle_render_document(handle, context, C.byref(viewport), C.byref(error)):
            raise SystemExit("SVG render failed")
        if cairo.cairo_surface_write_to_png(surface, str(prefix.with_suffix(".png")).encode()) != 0:
            raise SystemExit("PNG write failed")
    finally:
        cairo.cairo_destroy(context)
        cairo.cairo_surface_destroy(surface)
        gobject.g_object_unref(handle)
    with Image.open(prefix.with_suffix(".png")) as image:
        rgb = image.convert("RGB").tobytes()
    prefix.with_suffix(".rgb").write_bytes(rgb)
    provenance = {
        "source": "assets/branding/spiffos-logo.svg", "width": 400, "height": 160,
        "format": "RGB24 row-major, no padding", "background": "#091b35",
        "background_policy": "Owner-approved uniform boot backdrop; original SVG unchanged",
        "source_sha256": hashlib.sha256(svg.read_bytes()).hexdigest(),
        "rgb_sha256": hashlib.sha256(rgb).hexdigest(),
        "renderer": "host librsvg/Cairo via render-boot-logo.py",
        "font": subprocess.check_output(["fc-match", "DejaVu Sans:weight=bold"], text=True).strip()
    }
    prefix.with_suffix(".json").write_text(json.dumps(provenance, indent=2) + "\n")
    print("Rendered original logo with uniform boot backdrop to 400x160 RGB asset:", provenance["rgb_sha256"])


if __name__ == "__main__":
    main()

"""
UI Icon Pipeline: SVG Vector to Game-Ready Lanczos PNG
======================================================
Pipeline konwersji i importu wektorowych ikon statusów żywiołów (SVG)
do formatu rastrowego PNG (128x128) z próbkowaniem studyjnym Lanczos.

Status:
- WET (water-drop.svg) -> UKOŃCZONE i przetestowane w silniku (idealna ostrość)
- BURNING (fire.svg / flame.svg) -> DO DOKOŃCZENIA
- OILED (oil-drop.svg / droplet.svg) -> DO DOKOŃCZENIA
- ELECTRIFIED (lightning.svg) -> DO DOKOŃCZENIA

Wymagania:
- resvg-py (zainstalowane w środowisku Python Unreal Engine)
- Pillow (PIL)
"""

import os
import io
import resvg_py
from PIL import Image

# Katalogi źródłowe i docelowe
SOURCE_DIR = r"C:\Users\Sebastian\Downloads\status icons"
OUTPUT_DIR = r"E:\UE_PROJECTS\MyProject\Content\UI\Icons"

# Mapowanie plików SVG na docelowe PNG używane w Unreal Engine
ICON_MAPPINGS = {
    "wet": {
        "svg_relative": r"wet\water-drop.svg",
        "png_name": "wet_status_icon.png",
        "status": "COMPLETED"
    },
    "burning": {
        "svg_relative": r"burning\burning.svg",  # dopasować do nazwy pobranego pliku
        "png_name": "burning_status_icon.png",
        "status": "TODO"
    },
    "oiled": {
        "svg_relative": r"oiled\oiled.svg",      # dopasować do nazwy pobranego pliku
        "png_name": "oiled_status_icon.png",
        "status": "TODO"
    },
    "electrified": {
        "svg_relative": r"electrified\electrified.svg",  # dopasować do nazwy pobranego pliku
        "png_name": "electrified_status_icon.png",
        "status": "TODO"
    }
}

def convert_svg_to_lanczos_png(
    svg_path: str,
    output_png_path: str,
    target_size: int = 128,
    supersample_factor: int = 8
) -> bool:
    """
    Renderuje wektor SVG za pomocą silnika resvg do bufora o rozmiarze
    (target_size * supersample_factor), a następnie przeprowadza
    supersampling Lanczos do docelowego rozmiaru (target_size).

    Rozmiar viewBox źródłowego SVG jest niezależny od tego procesu —
    resvg zawsze skaluje wektor do zadanej rozdzielczości renderowania.
    """
    if not os.path.exists(svg_path):
        print(f"[BŁĄD] Nie odnaleziono pliku SVG: {svg_path}")
        return False

    with open(svg_path, "r", encoding="utf-8") as f:
        svg_content = f.read()

    # 1. Renderowanie wektora z wysokim współczynnikiem supersamplingu
    render_size = target_size * supersample_factor
    png_bytes = resvg_py.svg_to_bytes(svg_content, width=render_size, height=render_size)
    im_render = Image.open(io.BytesIO(png_bytes))

    # 2. Studyjny downsampling Lanczos do docelowego rozmiaru
    im_target = im_render.resize((target_size, target_size), Image.Resampling.LANCZOS)

    # 3. Zapis pliku PNG
    os.makedirs(os.path.dirname(output_png_path), exist_ok=True)
    im_target.save(output_png_path)
    print(f"[SUKCES] Wygenerowano ostry PNG ({target_size}x{target_size}) "
          f"z renderu {render_size}x{render_size}: {output_png_path}")
    return True

if __name__ == "__main__":
    print("--- UI Icon Processing Tool ---")
    for key, data in ICON_MAPPINGS.items():
        svg_full = os.path.join(SOURCE_DIR, data["svg_relative"])
        out_full = os.path.join(SOURCE_DIR, key, data["png_name"])
        print(f"Przetwarzanie {key.upper()} (Status: {data['status']})...")
        if os.path.exists(svg_full):
            convert_svg_to_lanczos_png(svg_full, out_full)
        else:
            print(f"  Pomiń (brak pliku SVG: {svg_full})")

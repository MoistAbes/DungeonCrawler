# Pipeline Importu i Optymalizacji Ikon Statusów UI

Dokumentacja techniczna procesu konwersji i importu ikon statusów żywiołów do Unreal Engine.

---

## 1. Architektura i Ustawienia

* **Źródło:** Pliki wektorowe **`.svg`** (np. z generatora `game-icons.net` w formacie 512–1024 px).
* **Problem małych rozmiarów:** Bezpośredni eksport małych PNG z generatorów www lub przypadkowych konwerterów online powoduje dziury w obrysie i poszarpane krawędzie (aliasing).
* **Rozwiązanie (Złoty Standard):** Renderowanie wektora SVG silnikiem **`resvg`** do bufora 512x512, a następnie studyjny downsampling **Lanczos** do rozmiaru potęgi dwójki (**128x128 px**).
* **Rozmiar w grze (`WBP_StatusIcon`):** `SizeBox` = **`48x48`** lub **`44x44` px** (zapewnia wystarczająco dużo fizycznych pikseli matrycy 1080p, by łuki i detale były ostre i czytelne).

---

## 2. Narzędzie automatyzacji

Stworzono dedykowany skrypt Pythona:
[`Tools/UI/Convert_Status_Icons.py`](file:///E:/UE_PROJECTS/MyProject/Tools/UI/Convert_Status_Icons.py)

### Uruchomienie skryptu:
```powershell
& "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" "E:\UE_PROJECTS\MyProject\Tools\UI\Convert_Status_Icons.py"
```

---

## 3. Status ikon

| Status efektu | Plik źródłowy SVG | Docelowy plik PNG w UE | Stan prac |
| :--- | :--- | :--- | :--- |
| **WET** | `Downloads/status icons/wet/water-drop.svg` | `wet_status_icon.png` | **UKOŃCZONE** (zweryfikowane w grze, idealna ostrość) |
| **BURNING** | `Downloads/status icons/burning/...` | `burning_status_icon.png` | **DO DOKOŃCZENIA** (pliki SVG pobrane) |
| **OILED** | `Downloads/status icons/oiled/...` | `oiled_status_icon.png` | **DO DOKOŃCZENIA** (pliki SVG pobrane) |
| **ELECTRIFIED** | `Downloads/status icons/electrified/...` | `electrified_status_icon.png` | **DO DOKOŃCZENIA** (pliki SVG pobrane) |

---

## 4. Ustawienia tekstury w Unreal Engine (Content/UI/Icons/)

Po zaimportowaniu / reimportowaniu PNG do UE tekstura musi mieć:
* **`Compression Settings`**: `TC_EditorIcon` (lub `TC_Default` bez kompresji stratnej)
* **`LOD Group`**: `TEXTUREGROUP_UI`
* **`Mip Gen Settings`**: `TMGS_FROM_TEXTURE_GROUP`
* **`Filter`**: `TF_Default` (lub `TF_Bilinear`)
* **`sRGB`**: `True`

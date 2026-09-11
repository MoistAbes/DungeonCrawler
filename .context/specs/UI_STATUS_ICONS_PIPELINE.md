# Pipeline Graficzny i Konfiguracja Ikon Statusu (Status Effects UI)

Niniejszy dokument opisuje kompletny, powtarzalny proces przygotowania, naprawy krawędzi (Alpha Bleeding / Fringing) oraz integracji ikon statusów żywiołowych w projekcie.

---

## 1. Wytyczne Graficzne dla Nowych Ikon
* **Rozmiar:** PNG 512x512 z przezroczystością (RGBA).
* **Styl:** Okrągła ikona w ciemnym obrysie: `RGB = (36, 42, 50)`.
* **Katalog docelowy w UE:** `/Game/UI/Icons/` (np. `burning_status_icon`, `wet_status_icon`, `electrified_status_icon`, `oiled_status_icon`).

---

## 2. Problem Alpha Bleed / Fringing (Biała Poświata)
### Dlaczego powstaje poświata?
Generatory graficzne (AI, Canva, Photoshop, serwisy wycinające tło) w pikselach przezroczystych (`Alpha = 0`) domyślnie zapisują czystą biel: `RGB = (255, 255, 255)`.  
Gdy Unreal Engine pomniejsza teksturę na ekranie (np. z 512px do 45px), silnik filtruje i miksuje sąsiednie piksele. Mieszanie ciemnego obrysu z ukrytą bielą powoduje powstanie jasnej, poszarpanej poświaty wokół koła.

### Rozwiązanie (Skrypt naprawiający)
Przed importem lub przed zatwierdzeniem ikony należy podmienić ukryte wartości RGB w pikselach z `Alpha == 0` na ciemny kolor obwódki `RGB = (36, 42, 50)`.

```bash
& "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" -c "
import zlib, struct, os

path = r'<SCIEZKA_DO_PLIKU_PNG>'

with open(path, 'rb') as f:
    sig = f.read(8)
    idat = b''
    while True:
        chunk = f.read(8)
        if not chunk: break
        length, ctype = struct.unpack('>I4s', chunk)
        data = f.read(length)
        f.read(4)
        if ctype == b'IDAT': idat += data
        elif ctype == b'IEND': break

decomp = zlib.decompress(idat)
stride = 1 + 512 * 4
unfiltered = bytearray(512 * 512 * 4)

for y in range(512):
    filter_type = decomp[y * stride]
    scanline_start = y * stride + 1
    dest_start = y * 512 * 4
    prev_dest_start = (y - 1) * 512 * 4
    for i in range(512 * 4):
        raw = decomp[scanline_start + i]
        a = unfiltered[dest_start + i - 4] if i >= 4 else 0
        b = unfiltered[prev_dest_start + i] if y > 0 else 0
        c = unfiltered[prev_dest_start + i - 4] if (y > 0 and i >= 4) else 0
        if filter_type == 0: val = raw
        elif filter_type == 1: val = (raw + a) & 0xFF
        elif filter_type == 2: val = (raw + b) & 0xFF
        elif filter_type == 3: val = (raw + ((a + b) // 2)) & 0xFF
        elif filter_type == 4:
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            val = (raw + pr) & 0xFF
        else: val = raw
        unfiltered[dest_start + i] = val

border_r, border_g, border_b = 36, 42, 50
fixed = 0
for y in range(512):
    for x in range(512):
        idx = (y * 512 + x) * 4
        if unfiltered[idx + 3] == 0:
            unfiltered[idx] = border_r
            unfiltered[idx + 1] = border_g
            unfiltered[idx + 2] = border_b
            fixed += 1

raw_scanlines = bytearray()
for y in range(512):
    raw_scanlines.append(0)
    raw_scanlines.extend(unfiltered[y * 512 * 4 : (y + 1) * 512 * 4])

compressed_idat = zlib.compress(bytes(raw_scanlines), level=9)

def make_chunk(ctype, data):
    return struct.pack('>I4s', len(data), ctype) + data + struct.pack('>I', zlib.crc32(ctype + data))

png_bytes = b'\x89PNG\r\n\x1a\n'
ihdr = struct.pack('>IIBBBBB', 512, 512, 8, 6, 0, 0, 0)
png_bytes += make_chunk(b'IHDR', ihdr)
png_bytes += make_chunk(b'IDAT', compressed_idat)
png_bytes += make_chunk(b'IEND', b'')

with open(path, 'wb') as f:
    f.write(png_bytes)

print(f'Poprawiono {fixed} pikseli transparentnych na ciemny kolor obrysu!')
"
```

---

## 3. Ustawienia Tekstury w Unreal Engine (Texture Details)
Po imporcie lub reimporcie tekstury do `/Game/UI/Icons/`, w panelu **Details** assetu należy bezwzględnie ustawić:

1. **`LOD Group`** $\rightarrow$ **`UI`**
2. **`Compression Settings`** $\rightarrow$ **`Uncompressed (RGBA8)`** (`TC_EDITOR_ICON`)
3. **`Mip Gen Settings`** $\rightarrow$ **`Sharpen1`** (`TMGS_SHARPEN1`)
4. **`Never Stream`** $\rightarrow$ **`True`** (zaznaczony)

---

## 4. Rejestracja Ikony w UI
1. Otwórz Blueprint widżetu: `/Game/UI/WBP_StatusIcon`.
2. Kliknij **Class Defaults** na górnej belce.
3. W sekcji **Config** w mapie **`Status Textures`** kliknij `+` (Add Element):
   * **Klucz:** Wybierz status z enuma (np. `Burning`, `Wet`, `Electrified`, `Oiled` lub nowo dodany).
   * **Wartość:** Wybierz zaimportowaną teksturę.
4. Kliknij **Compile** i **Save**.

---

## 5. Konfiguracja Rozmiaru Ikon na Ekranie
Wszystkie ikony statusów są instancjami widżetu `WBP_StatusIcon`:
* Aby zmienić ich rozmiar w grze (np. na `30x30`, `45x45`, `56x56`):
  1. Otwórz `/Game/UI/WBP_StatusIcon`.
  2. W drzewie hierarchii zaznacz `SizeBox_35`.
  3. Zmień **`Width Override`** i **`Height Override`**.
* Kontener w `WBP_PlayerHUD` (`StatusEffectsContainer`) ma włączone **`Auto Size = True`**, więc automatycznie dopasuje swoje wymiary do dowolnej wielkości i liczby ikonek.

# Kompletny Przewodnik: Edycja Mapy i Generowanie Poziomów przez Unreal MCP

> **Przeznaczenie:** Dokumentacja techniczna i podręcznik operacyjny dla użytkownika oraz asystentów AI w przyszłych sesjach, opisujący jak zarządzać poziomami, rozmieszczać modularne fundamenty, generować architekturę lochu i manipulować aktorami w **Unreal Engine 5** przy użyciu **Unreal MCP**.

---

## ⚡ SZYBKI START DLA ASYSTENTA AI (OD STRZAŁA)

> [!IMPORTANT]
> **NIE SZUKAJ NARZĘDZI W NAGŁÓWKACH C++ ANI NIE SKANUJ PLIKÓW SILNIKA!**
> Wszystkie narzędzia edycji sceny, aktorów i assetów są już w pełni aktywne w edytorze UE 5.8 w module `editor_toolset`.
> Aby uniknąć problemów z wygasaniem sesji HTTP Streamable MCP po restarcie edytora, w projekcie stworzono **gotowy mostek CLI**:
> [`Tools/MCP/unreal_mcp.py`](file:///E:/UE_PROJECTS/MyProject/Tools/MCP/unreal_mcp.py).
> Zawsze uruchamiaj go dedykowanym interpreterem Pythona z silnika:
> `& "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Tools/MCP/unreal_mcp.py <komenda>`

### Trzy podstawowe komendy robocze:

1. **Sprawdzenie statusu i załadowanej mapy (Zacznij od tego w nowej sesji!):**
   ```powershell
   & "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Tools/MCP/unreal_mcp.py status
   ```
   *Zwraca w 1 sekundę:* `{"status": "ONLINE", "current_level": "/Game/Maps/Map_Dungeon_01"}`.

2. **Wykonanie wsadowego skryptu generowania/modyfikacji mapy:**
   Przygotuj skrypt z funkcją `run()` (szablon w Sekcji 4 tego dokumentu) i wywołaj:
   ```powershell
   & "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Tools/MCP/unreal_mcp.py run-script sciezka/do/skryptu.py
   ```
   *Skrypt wykonuje się bezpośrednio w procesie Unreal Editora w ułamku sekundy przez `ProgrammaticToolset` z transakcją Undo.*

3. **Utrwalenie zmian na dysku (Zapisanie pliku `.umap`):**
   ```powershell
   & "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Tools/MCP/unreal_mcp.py save-level
   ```

---

## 1. Wprowadzenie i Architektura MCP

Unreal Engine MCP (Model Context Protocol) łączy asystenta AI bezpośrednio z żywym, otwartym edytorem Unreal Engine. Umożliwia to wykonywanie operacji na scenie w czasie rzeczywistym — bez konieczności ręcznego przeciągania setek assetów w edytorze.

### Wymagania wstępne:
1. **Unreal Editor uruchomiony z projektem `MyProject`**:
   - Edytor nasłuchuje lokalnie na porcie **`http://127.0.0.1:8000/mcp`**.
2. **Aktywny poziom (Level)**:
   - Przed wykonaniem operacji upewnij się, na jakiej mapie pracujesz (`Tools/MCP/unreal_mcp.py status`).

---

## 2. Kluczowe Zestawy Narzędzi (Toolsets) i Funkcje

Narzędzia MCP są podzielone na moduły (toolsety). Do edycji mapy wykorzystujemy głównie poniższe 4 zestawy:

### A. `editor_toolset.toolsets.scene.SceneTools`
Zarządzanie sceną, poziomem, folderami Outlinera i umieszczaniem aktorów.
- **`get_current_level`** – Pobiera ścieżkę do aktywnej mapy.
- **`load_level(level_path)`** – Ładuje podaną mapę do edytora (np. `"/Game/Maps/Map_Dungeon_01"`).
- **`find_actors(name, tag, collision_channels)`** – Wyszukuje aktorów w świecie po nazwie lub tagu.
- **`get_actors_in_folder(folder_path, recursive)`** – Zwraca listę referencji aktorów z danego folderu w World Outlinerze.
- **`set_actor_folder(actor, folder_path)`** – Przypisuje aktora do folderu w Outlinerze (tworzy strukturę folderów automatycznie, jeśli nie istnieje).
- **`add_to_scene_from_asset(asset_path, name, xform, parent, snap_to_ground)`** – Wstawia do sceny aktora na podstawie assetu (np. Blueprinta lub Static Mesha).
- **`add_to_scene_from_class(actor_type, name, xform, parent, snap_to_ground)`** – Wstawia do sceny aktora czystej klasy C++ lub wbudowanej (np. `PointLight`, `PlayerStart`).
- **`remove_from_scene(actor)`** – Usuwa wskazanego aktora ze sceny (uwaga: parametr wejściowy to `"actor"`, nie `"actors"`).

### B. `editor_toolset.toolsets.actor.ActorTools`
Manipulacja transformacją i właściwościami konkretnych aktorów.
- **`get_actor_transform(actor)`** – Pobiera pozycję (`location`), rotację (`rotation`) i skalę (`scale`).
- **`set_actor_transform(actor, xform)`** – Ustawia współrzędne aktora (uwaga: parametr wejściowy to `"xform"`, nie `"transform"`).
- **`get_actor_bounds(actor)`** – Zwraca prostopadłościan kolizji/rozmiarów aktora (`origin`, `box_extent`).
- **`get_components(actor)`** – Zwraca listę komponentów aktora.
- **`get_label(actor)`** – Pobiera wyświetlaną nazwę aktora w Outlinerze.

### C. `editor_toolset.toolsets.asset.AssetTools`
Zarządzanie plikami uasset i zapisywanie stanu mapy.
- **`save_assets(asset_paths)`** – **KRYTYCZNE:** Zapisuje zmodyfikowaną mapę na dysk (np. `["/Game/Maps/Map_Dungeon_01"]`). Bez tego zmiany pozostaną tylko w pamięci podręcznej i znikną przy restarcie edytora!
- **`find_assets(query, asset_types)`** – Szuka assetów w Content Browser.
- **`duplicate(source_path, destination_path)`** – Duplikuje asset (np. klonuje mapę bazową do nowej).

### D. `editor_toolset.toolsets.programmatic.ProgrammaticToolset`
**NAJWAŻNIEJSZE I NAJSZYBSZE NARZĘDZIE DO GENEROWANIA MAPY:**
- **`execute_tool_script(script)`** – Umożliwia uruchomienie kompletnego skryptu Pythona bezpośrednio w procesie edytora.
  - Zamiast wysyłać 400 pojedynczych zapytań HTTP (co trwałoby kilka minut i mogłoby zerwać połączenie), skrypt Pythona wykonuje całe generowanie lokalnie w silniku w **kilka sekund**!

### E. `editor_toolset.toolsets.material_instance.MaterialInstanceTools`
Zarządzanie parametrami instancji materiałów (błyskawiczne dostosowywanie kolorów i właściwości powierzchni w locie):
- **`list_parameters(material)`** – Zwraca nazwy i typy parametrów wystawionych przez materiał (np. `"BaseColor"`).
- **`get_vector_parameter(instance, name)`** – Pobiera obecną wartość koloru wektorowego (`LinearColor` RGBA).
- **`set_vector_parameter(instance, name, value)`** – Zmienia kolor wektora (np. `{"r": 0.25, "g": 0.25, "b": 0.26, "a": 1.0}`). Zmiana w instancji materiału natychmiast odświeża wszystkie obiekty w świecie gry bez rekompilacji shaderów.
- **`set_scalar_parameter(instance, name, value)`** – Zmienia parametry liczbowe (np. chropowatość, metaliczność).

### F. `editor_toolset.toolsets.blueprint.BlueprintTools`
Zarządzanie Blueprintami, ich kompilacją i obiektami domyślnymi:
- **`get_default_object(blueprint)`** – Pobiera referencję do Class Default Object (CDO) danego Blueprinta (`{"refPath": "...Default__BP_Name_C"}`).
- **`compile_blueprint(blueprint, warnings_as_errors)`** – Kompiluje wskazany Blueprint po modyfikacji właściwości lub grafu.
- **`create(folder_path, asset_name, asset_type)`** – Tworzy nowy pusty Blueprint w podanym folderze.

### G. `editor_toolset.toolsets.object.ObjectTools`
Inspekcja i bezpośrednia modyfikacja właściwości obiektów silnika (w tym pól w CDO i komponentach):
- **`get_properties(instance, properties)`** – Zwraca wartości wskazanych pól obiektu w formacie JSON (`properties` to lista stringów).
- **`set_properties(instance, values)`** – Ustawia wartości pól na obiekcie. Argument `values` to string JSON zawierający słownik `{klucz: wartość}`.
- **`list_properties(instance)`** – Zwraca listę wszystkich dostępnych właściwości obiektu/komponentu.

---

## 3. Standardy Siatki i Modułów (Dungeon Grid Conventions)

Wszystkie fundamenty lochu w projekcie `MyProject` oparte są na siatce modularnej:

| Parametr | Wartość | Wyjaśnienie |
| :--- | :--- | :--- |
| **Krok Siatki (Grid Step)** | **`300 cm`** ($3\text{ metry}$) | Standardowy rozmiar kafelka podłogi i szerokości ściany. |
| **Wysokość 1 Bloku** | **`300 cm`** ($3\text{ metry}$) | Standardowa wysokość podstawowej ściany. |
| **Wysokość Lochu (Dungeon 01)** | **`900 cm`** ($9\text{ metrów}$ / 3 bloki) | Wysokie sklepienie katedralne, skala ściany $Z = 9.0$. |
| **Poziom Posadzki (Floor Z)** | **`Z = 20 cm`** | Górne lico podłogi. Środek aktora kafelka: `Z = 10 cm`, grubość `20 cm`, `Roll = 90°`. |
| **Podstawa Ścian i Kolumn** | **`Z = 20 cm`** | Ściany i kolumny opierają się dokładnie na posadzce. |
| **Środek Ścian (dla 9m)** | **`Z = 470 cm`** | $20\text{ cm} + \frac{900\text{ cm}}{2} = 470\text{ cm}$. Skala pionowa: $Z = 9.0$. |
| **Poziom Sufitu (Ceiling Z)** | **`Z = 920 cm`** | Dolne lico sufitu. Środek aktora sufitu: `Z = 930 cm`, grubość `20 cm`, `Roll = 90°`. |
| **Poziom Piętra / Antresoli (Mezzanine Z)** | **`Z = 320 cm`** | Górne lico antresoli/balkonu. Środek aktora: `Z = 310 cm`, grubość `20 cm`, `Roll = 90°`. |
| **Rampa Wjazdowa na Piętro (Ramp)** | **`Pitch = 26.565°`** | Wznios $300\text{ cm}$ na odcinku $600\text{ cm}$ (2 moduły). Skala: $X = 6.7082$ (długość $670.8\text{ cm}$), $Y = 3.0$ (szerokość $3\text{ metry}$), $Z = 0.2$. Środek $Z = 170\text{ cm}$. Kąt $26.6^\circ$ jest całkowicie bezpieczny dla wspinania postaci (limit UE: $44.76^\circ$). |

### Standardowe Assety w Projekcie:
- **Posadzka**: `/Game/Dungeon/Structures/BP_DungeonWall_Solid` (położony na płasko: `Roll = 90°`, grubość `0.2`)
- **Pęknięta Posadzka (Zniszczalna)**: `/Game/Dungeon/Structures/BP_DungeonFloor_Cracked`
- **Ściana Kamienna**: `/Game/Dungeon/Structures/BP_DungeonWall_Solid`
- **Barykada Drewniana**: `/Game/Dungeon/Structures/BP_DungeonWall_Wood`
- **Ściana Szklana (Krucha / Zniszczalna)**: `/Game/Dungeon/Structures/BP_DungeonWall_Glass` (`MaterialType = Glass`, `bIsDestructible = true`, `PunchThrough = 0.85`, `Durability = 40.0`)
- **Instancje Materiałów Architektur (`MaterialInstanceConstant`)**:
  - **Kamień (`Stone`)**: `/Game/Materials/MI_Stone_Grey` (szary `BaseColor`: `0.25, 0.25, 0.26`)
  - **Kamień Spękany (`Stone Cracked`)**: `/Game/Materials/MI_Stone_Cracked` (grafitowy `BaseColor`: `0.12, 0.12, 0.13`)
  - **Drewno (`Wood`)**: `/Game/Materials/MI_Wood_Brown` (ciepły brąz `BaseColor`: `0.25, 0.10, 0.03`)
  - **Szkło (`Glass`)**: `/Game/Materials/MI_Glass_Window` (chłodny błękitno-turkusowy `Color`: `0.15, 0.65, 0.85, 1.0`, `Opacity`: `0.35`)
- **Szklane Bomby Żywiołowe**:
  - Ogień: `/Game/Interactive/Bombs/BP_Bomb_Fire`
  - Oliwa: `/Game/Interactive/Bombs/BP_Bomb_Oil`
  - Elektryczność: `/Game/Interactive/Bombs/BP_Bomb_Electric`
  - Woda: `/Game/Interactive/Bombs/BP_Bomb_Water`
  - Odrzut: `/Game/Interactive/Bombs/BP_Bomb_Knockback`
- **Interaktywne Rekwizyty**:
  - Dźwignia: `/Game/Interactive/Props/BP_Switch_Lever`
  - Wybuchowa Beczka: `/Game/Interactive/Props/BP_ExplosiveBarrel`
  - Beczka z Oliwą: `/Game/Interactive/Props/BP_OilBarrel`
  - Beczka Elektryczna: `/Game/Interactive/Props/BP_ElectricBarrel`

---

## 4. Wzorzec Skryptu Generującego (Szablon Python dla `execute_tool_script`)

Poniższy skrypt jest kompletnym, przetestowanym wzorcem do masowej generacji sal, korytarzy, kolumn i oświetlenia:

```python
import json

def spawn_asset(asset_path, name, x, y, z, pitch=0.0, yaw=0.0, roll=0.0, sx=1.0, sy=1.0, sz=1.0, folder=""):
    xform = {
        "location": {"x": float(x), "y": float(y), "z": float(z)},
        "rotation": {"pitch": float(pitch), "yaw": float(yaw), "roll": float(roll)},
        "scale": {"x": float(sx), "y": float(sy), "z": float(sz)}
    }
    payload = {
        "asset_path": asset_path,
        "name": name,
        "xform": xform,
        "parent": None,
        "snap_to_ground": False
    }
    res = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps(payload))
    actor = res.get("returnValue")
    if folder and actor:
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({"actor": actor, "folder_path": folder}))
    return actor

def spawn_class(class_path, name, x, y, z, pitch=0.0, yaw=0.0, roll=0.0, sx=1.0, sy=1.0, sz=1.0, folder=""):
    xform = {
        "location": {"x": float(x), "y": float(y), "z": float(z)},
        "rotation": {"pitch": float(pitch), "yaw": float(yaw), "roll": float(roll)},
        "scale": {"x": float(sx), "y": float(sy), "z": float(sz)}
    }
    payload = {
        "actor_type": {"refPath": class_path},
        "name": name,
        "xform": xform,
        "parent": None,
        "snap_to_ground": False
    }
    res = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class", json.dumps(payload))
    actor = res.get("returnValue")
    if folder and actor:
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({"actor": actor, "folder_path": folder}))
    return actor

def cleanup_previous_generation(parent_folder="Dungeon_Generated"):
    """Usuwa poprzednio wygenerowane obiekty, aby uniknąć duplikatów."""
    subfolders = ["Floors", "Ceilings", "Walls", "Pillars", "Barricades", "GlassBombs", "Props", "Lighting", "Gameplay"]
    for sub in subfolders:
        path = f"{parent_folder}/{sub}"
        try:
            res = execute_tool("editor_toolset.toolsets.scene.SceneTools.get_actors_in_folder", json.dumps({"folder_path": path, "recursive": True}))
            actors = res["returnValue"] if "returnValue" in res else []
            for a in actors:
                execute_tool("editor_toolset.toolsets.scene.SceneTools.remove_from_scene", json.dumps({"actor": a}))
        except Exception:
            pass

def run():
    # 1. Czyszczenie poprzedniej architektury (opcjonalne)
    cleanup_previous_generation()

    # Foldery tworzą się automatycznie podczas wywoływania SceneTools.set_actor_folder!
    counts = {"floors": 0, "walls": 0, "lights": 0}

    # Ścieżki assetów
    wall_solid = "/Game/Dungeon/Structures/BP_DungeonWall_Solid"
    floor_cracked = "/Game/Dungeon/Structures/BP_DungeonFloor_Cracked"

    # 3. Układanie posadzki na siatce (np. pokój 4x4)
    # Rozmiar kafelka to 300x300 cm
    for gx in range(4):
        for gy in range(4):
            x = gx * 300.0
            y = gy * 300.0
            # Posadzka (Z = 10 cm, grubość = 0.2, Roll = 90 deg)
            spawn_asset(wall_solid, f"Floor_{gx}_{gy}", x, y, 10.0, roll=90.0, sx=3.0, sy=0.2, sz=3.0, folder="Dungeon_Generated/Floors")
            # Sufit (Z = 930 cm, zamknięcie lochu)
            spawn_asset(wall_solid, f"Ceiling_{gx}_{gy}", x, y, 930.0, roll=90.0, sx=3.0, sy=0.2, sz=3.0, folder="Dungeon_Generated/Ceilings")
            counts["floors"] += 1

    # 4. Dodawanie oświetlenia (PointLight)
    light = spawn_class("/Script/Engine.PointLight", "RoomLight_Center", 450.0, 450.0, 350.0, folder="Dungeon_Generated/Lighting")
    if light:
        counts["lights"] += 1

    return {"status": "SUCCESS", "counts": counts}
```

### 4.2. Wzorzec Skryptu: Klonowanie i Konfiguracja Blueprintów oraz Materiałów (Szablon Python)

Gdy potrzebujesz stworzyć nowy wariant struktury (np. szklaną ścianę ze specjalnymi właściwościami zniszczeń, innym materiałem i wytrzymałością):

```python
import json

def run():
    new_bp_path = "/Game/Dungeon/Structures/BP_DungeonWall_Glass"
    source_bp_path = "/Game/Dungeon/Structures/BP_DungeonWall_Wood"

    # 1. Sprawdzenie i ewentualne usunięcie starego assetu
    exists_res = execute_tool("editor_toolset.toolsets.asset.AssetTools.exists", json.dumps({"path": new_bp_path}))
    if "returnValue" in exists_res and exists_res["returnValue"]:
        execute_tool("editor_toolset.toolsets.asset.AssetTools.delete", json.dumps({"path": new_bp_path}))

    # 2. Sklonowanie bazowego Blueprinta
    execute_tool("editor_toolset.toolsets.asset.AssetTools.duplicate", json.dumps({
        "path": source_bp_path,
        "new_path": new_bp_path
    }))

    # 3. Pobranie Class Default Object (CDO)
    cdo_res = execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.get_default_object", json.dumps({
        "blueprint": {"refPath": f"{new_bp_path}.BP_DungeonWall_Glass"}
    }))
    cdo_ref = cdo_res["returnValue"]

    # 4. Ustawienie zmiennych klasy na CDO (MaterialType, bIsDestructible, PunchThrough, itp.)
    cdo_vals = json.dumps({
        "MaterialType": "Glass",
        "bIsDestructible": True,
        "PunchThroughVelocityRetention": 0.85
    })
    execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
        "instance": cdo_ref,
        "values": cdo_vals
    }))

    # 5. Ustawienie właściwości komponentów podrzędnych (np. OverrideMaterials w StructureMesh)
    # Składnia referencji: {asset_path}.Default__{asset_name}_C:{component_name}
    mesh_ref = {"refPath": f"{new_bp_path}.Default__BP_DungeonWall_Glass_C:StructureMesh"}
    mesh_vals = json.dumps({
        "OverrideMaterials": [{"refPath": "/Game/Materials/MI_Glass_Window.MI_Glass_Window"}]
    })
    execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
        "instance": mesh_ref,
        "values": mesh_vals
    }))

    # 6. Ustawienie parametrów komponentu DamageableComponent (punkty wytrzymałości)
    dmg_ref = {"refPath": f"{new_bp_path}.Default__BP_DungeonWall_Glass_C:DamageableComponent"}
    dmg_vals = json.dumps({
        "MaxDurability": 40.0,
        "InitialDurability": 40.0,
        "bIsInvulnerable": False
    })
    execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
        "instance": dmg_ref,
        "values": dmg_vals
    }))

    # 7. Kompilacja Blueprinta (niezbędna, aby zmiany weszły w życie!)
    execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.compile_blueprint", json.dumps({
        "blueprint": {"refPath": f"{new_bp_path}.BP_DungeonWall_Glass"}
    }))

    # 8. Trwały zapis assetu na dysku
    execute_tool("editor_toolset.toolsets.asset.AssetTools.save_assets", json.dumps({
        "asset_paths": [new_bp_path]
    }))

    return {"status": "SUCCESS", "asset": new_bp_path}
```

---

## 5. Procedura Krok po Kroku dla Nowej Sesji

Gdy w nowej sesji chcesz zmodyfikować lub rozbudować mapę:

### Krok 1: Weryfikacja Połączenia i Stanu Świata
Poproś asystenta:
> *"Sprawdź aktualnie załadowaną mapę w Unreal Engine przez MCP."*
- Asystent wywołuje `get_current_level`.
- Jeśli edytor jest zamknięty lub sesja wygasła, asystent poinformuje o stanie połączenia.

### Krok 2: Zdefiniowanie Założeń Architektonicznych
Wystarczy krótki, czytelny opis, np.:
> *"Rozbuduj mapę `Map_Dungeon_01`. Dodaj korytarz na północ o długości 5 kafelków, a na jego końcu skarbiec 3x3 z ołtarzem i 2 pochodniami."*

### Krok 3: Wygenerowanie i Walidacja
- Asystent generuje geometrię za pomocą skryptu `execute_tool_script` (spasowanie siatki $300\text{ cm}$, podniesienie kolumn i sufitu).
- Wszystkie obiekty trafiają do czytelnej struktury folderów `Dungeon_Generated/...` w World Outlinerze.
- Asystent automatycznie zapisuje mapę na dysk (`save_assets`).

### Krok 4: Test w Edytorze
- W oknie edytora Unreal Engine wciśnij **`Alt + P`** (Play In Editor).
- Przetestuj kolizje, oświetlenie i zachowanie bomb/rekwizytów.

---

## 6. Rozwiązywanie Problemów (Troubleshooting)

### Problem 1: Błąd `Unknown session id ... client should reinitialize`
- **Przyczyna:** Edytor Unreal Engine został zrestartowany lub zresetowano wtyczkę MCP, przez co stary identyfikator sesji wygasł.
- **Rozwiązanie:** Wystarczy przeładować sesję w narzędziu AI (lub wysłać nowe zapytanie, które nawiąże nowe połączenie `tools/call`). Upewnij się, że w pasku zadań działa proces `UnrealEditor.exe`.

### Problem 2: Zmiany na mapie zniknęły po restarcie edytora
- **Przyczyna:** Nie wywołano narzędzia `save_assets`.
- **Rozwiązanie:** Zawsze upewnij się, że na koniec operacji wykonano:
  ```python
  execute_tool("editor_toolset.toolsets.asset.AssetTools.save_assets", json.dumps({"asset_paths": ["/Game/Maps/Map_Dungeon_01"]}))
  ```

### Problem 3: Kolizje znikają lub ściany są przekręcone
- **Przyczyna:** Kąty Pitch/Yaw/Roll w transformacji.
- **Zasada:** 
  - Kafelki podłogowe z `BP_DungeonWall_Solid` wymagają `Roll = 90.0` (lub `Pitch = 90.0`) oraz skali grubości `sy = 0.2`.
  - Ściany pionowe wzdłuż osi X mają `Yaw = 0.0`.
  - Ściany pionowe wzdłuż osi Y mają `Yaw = 90.0`.

### Problem 4: W lochu jest za jasno mimo zamkniętego sufitu
- **Przyczyna:** Główne światło słoneczne (`DirectionalLight`) lub `SkyLight` w starszych wersjach mapy ma ustawione oświetlenie statyczne bez zbudowanego oświetlenia, lub ma włączony nienaturalnie wysoki parametr `Lower Hemisphere Color`.
- **Rozwiązanie:** W lochu podziemnym `DirectionalLight` i `SkyLight` powinny mieć wyłączoną intensywność we wnętrzu lub być wyłączone na mapie lochu, polegając w 100% na punktowych światłach `PointLight` / pochodniach z włączonym Lumenem.

### Problem 5: `TypeError: _StrictDict.get() does not support a default value`
- **Przyczyna:** W skryptach wykonywanych przez `execute_tool_script` wynik zwracany przez `execute_tool` jest instancją `_StrictDict`. Wywołanie metody `.get(key, default)` rzuca błąd `TypeError`.
- **Rozwiązanie:** Używaj bezpośredniego dostępu do klucza `res["returnValue"]` lub warunku `if "returnValue" in res: res["returnValue"]`.

### Problem 6: `Import of 'unreal' is not permitted. Allowed modules: ...`
- **Przyczyna:** Środowisko `ProgrammaticToolset` w edytorze działa w bezpiecznej piaskownicy Pythona i blokuje bezpośredni import `import unreal`. Dozwolone moduły standardowe to wyłącznie: `time`, `datetime`, `math`, `json`, `re`, `copy`.
- **Rozwiązanie:** Wszystkie operacje na obiektach silnika wykonuj za pośrednictwem funkcji `execute_tool("toolset.narzedzie", json.dumps(argumenty))`.

### Problem 7: `Parameter error: ... is not a valid object path for property 'instance'`
- **Przyczyna:** Przy modyfikacji właściwości Blueprinta za pomocą `ObjectTools.set_properties` przekazano ścieżkę do assetu Blueprinta (`/Game/.../BP_Nazwa`) zamiast obiektu instancji lub CDO.
- **Rozwiązanie:** 
  1. Właściwości klasy i zmienne domyślne modyfikuj na Class Default Object pobranym przez `BlueprintTools.get_default_object`.
  2. Komponenty podrzędne Blueprinta (np. siatki, komponenty obrażeń) adresuj pełną ścieżką podobiektu:  
     `f"{asset_path}.Default__{asset_name}_C:{component_name}"` (np. `/Game/Dungeon/Structures/BP_DungeonWall_Glass.Default__BP_DungeonWall_Glass_C:StructureMesh`).

### Problem 8: `TimeoutError: timed out` przy masowej generacji (200+ aktorów)
- **Przyczyna:** Domyślny timeout żądań HTTP w skrypcie mostka CLI wynosił 30 sekund. Tworzenie złożonej komnaty z kilkuset aktorami wraz z fizyką i oświetleniem w silniku zajmuje 40–70 sekund, przez co klient Pythona przedwcześnie zrywał połączenie mimo trwającej pracy w silniku.
- **Rozwiązanie:** W [`Tools/MCP/unreal_mcp.py`](file:///E:/UE_PROJECTS/MyProject/Tools/MCP/unreal_mcp.py) timeout został zwiększony do **180 sekund** (`timeout=180`), co pozwala na bezproblemowe, jednorazowe generowanie całych wielkich sal i pięter.

### Problem 9: Ciche zamykanie edytora przy uruchamianiu z PowerShell (Windows Job Object)
- **Przyczyna:** Standardowe uruchomienie edytora przez `Start-Process UnrealEditor.exe` w sesji terminala lub podprocesie agenta AI przypisuje Unreal Editor do tymczasowego **Windows Job Object**. Gdy polecenie powłoki się kończy, jądro Windows automatycznie zabija wszystkie procesy potomne powiązane z tym Job Objectem (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`). W logu widać: `LogMemory: Process is running as part of a Windows Job with separate resource limits` i po kilku sekundach edytor znika bez żadnego pliku crasha w `Saved/Crashes`.
- **Rozwiązanie:** Należy uruchomić proces edytora przez **WMI** (`Win32_Process.Create`), co odpina proces od drzewa procesów powłoki i uruchamia go w trwałym kontekście systemowym:
  ```powershell
  $cmd = 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "E:\UE_PROJECTS\MyProject\MyProject.uproject"'
  Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{CommandLine = $cmd}
  ```

### Problem 10: `Parameter error: ... is not a valid object path for property 'blueprint'`
- **Przyczyna:** `BlueprintTools.get_default_object` oraz `BlueprintTools.compile_blueprint` nie akceptują ścieżki pakietu assetu (np. `{"refPath": "/Game/Interactive/Props/BP_Switch_Lever"}`).
- **Rozwiązanie:** Należy podać pełną ścieżkę do obiektu Blueprinta z kropką:
  ```json
  {"blueprint": {"refPath": "/Game/Interactive/Props/BP_Switch_Lever.BP_Switch_Lever"}}
  ```

### Problem 11: Pułapki nazw parametrów w schematach narzędzi MCP (Strict Schema Gotchas)
Narzędzia MCP w Unreal walidują schematy JSON rygorystycznie. Użycie intuicyjnej nazwy zamiast dokładnej nazwy ze schematu skutkuje błędem `input param "..." is required by the function input schema Json, but is missing`:
- **`AssetTools.create_folder`**: Parametr to **`path`**, a NIE `folder_path`.
- **`ObjectTools.list_properties`**: Parametr to **`instance`**, a NIE `object`.
- **`ObjectTools.get_properties`**: Parametr to **`properties`** (tablica nazw string), a NIE `property_names`.
- **`StaticMeshTools.get_bounds`**: Parametr to **`mesh`**, a NIE `static_mesh`.
- **`SceneTools.find_actors`**: Wymaga przekazania wszystkich trzech pól oznaczonych w schemacie jako wymagane: `name`, `tag` oraz `collision_channels` (np. `{"name": "...", "tag": "", "collision_channels": []}`).

### Problem 12: Wyszukiwanie `find_actors` po `name` filtruje etykietę (`ActorLabel`), a nie nazwę instancji UObject
- **Przyczyna:** Wyszukiwanie `find_actors` z `name: "BP_DungeonGate_Portcullis"` nie zwraca wyników, jeśli etykieta aktora na poziomie to np. `Gate_Arena_Entrance` lub wygenerowane `Gate`.
- **Rozwiązanie:** Szukaj po fragmencie etykiety (np. `name: "Gate"`) lub pobieraj aktorów z dedykowanego folderu Outlinera za pomocą `SceneTools.get_actors_in_folder(folder_path, recursive=True)`.

### Problem 13: Łączenie aktorów referencjami (np. `TargetMechanisms`, tablice `TArray<AActor*>`)
- **Format:** Aby ustawić tablicę wskaźników do innych aktorów w scenie na instancji aktora (np. `TargetMechanisms` w płytach naciskowych i dźwigniach), przekaż listę obiektów `refPath`:
  ```python
  target_vals = {
      "targetMechanisms": [
          {"refPath": "/Game/Maps/Map_Dungeon_01.Map_Dungeon_01:PersistentLevel.BP_DungeonGate_Portcullis_C_0"}
      ]
  }
  execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
      "instance": plate_actor,
      "values": json.dumps(target_vals)
  }))
  ```

### Problem 14: `UnicodeEncodeError: 'charmap' codec can't encode character...` w Pythonie CLI
- **Przyczyna:** Konsola PowerShell w Windows domyślnie używa kodowania strony kodowej `cp1252` lub `cp852`. Wypisywanie polskich znaków diakrytycznych w instrukcjach `print(...)` w skryptach Pythona uruchamianych przez silnikowy `python.exe` powoduje natychmiastowe przerwanie skryptu błędem kodowania.
- **Rozwiązanie:** Używaj w komunikatach diagnostycznych skryptów MCP wyłącznie znaków ASCII (bez polskich ogonków) lub ustaw w środowisku `PYTHONIOENCODING=utf-8`.


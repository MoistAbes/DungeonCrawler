# Specyfikacja Techniczna i Projektowa: Theme Park (Loch v0.1)

## 1. Cel i Rola Dokumentu
Dokument stanowi **kompletną specyfikację projektowo-architektoniczną** poziomu testowego **Theme Park (Loch v0.1)** dla gry *Dungeon Crawler*.
Theme Park to ręcznie zbudowany, modularny poligon doświadczalny w Unreal Engine 5.8, którego zadaniem jest:
1. **Weryfikacja w boju (Battle Testing)** wszystkich dotychczas zaimplementowanych systemów: fizyki Chaos, kinetyki, blokowania tarczą, reakcji chemicznych żywiołów, zniszczeń struktur oraz autorytatywnego kodu sieciowego Co-op (1–6 graczy).
2. **Stworzenie produkcyjnych Blueprintów (Production-Ready Assets)**, które zastąpią dotychczasowe bryły zastępcze (szare prostopadłościany i cylindry) i wejdą w skład finalnej gry.
3. **Przygotowanie pierwszego pokoju kluczowego (Key Location Anchor)** pod kątem przyszłego algorytmu proceduralnego generowania lochów.

---

## 2. Standard Siatki Modularnej (Dungeon Grid & Dimensions)

Aby loch był kompatybilny z modułami proceduralnymi i zapewniał komfortową rozgrywkę kooperacyjną dla 1–6 graczy, przyjmujemy **metryczną siatkę klockową**:

| Element | Wymiary (X × Y × Z) | Uzasadnienie projektowe |
| :--- | :--- | :--- |
| **Klocek Podłogi / Sufitu** | **`400 × 400 × 20 cm`** | Jednostka podstawowa (Tile 4x4m). Pozwala na swobodny bieg 3 graczy obok siebie (kapsuła gracza: promień 42 cm, średnica 84 cm). |
| **Klocek Ściany Pełnej** | **`400 × 30 × 350 cm`** | Grubość 30 cm zapobiega przenikaniu kamer TPP i artefaktom oświetlenia Lumen. Wysokość 3.5m daje przestrzeń dla kamery i lotu propów. |
| **Wnęka Drzwiowa / Brama** | **`200 × 30 × 280 cm`** | Światło przejścia umożliwia przenoszenie szerokich skrzyń i rzucanie głazami bez blokowania o futrynę. |
| **Klocek Kolumny / Narożnika** | **`40 × 40 × 350 cm`** | Maskowanie łączeń ścian i punkty oparcia dla łuków sklepień. |

> **Zasada Pivot Point:** Wszystkie kafelki podłóg i ścian muszą mieć punkt bazowy (Pivot) wycentrowany w osi XY na krawędzi modułu lub w jego dolnym rogu na poziomie $Z = 0$, co gwarantuje natychmiastowe przyciąganie do siatki edytora (`Grid Snap = 50 / 100 cm`).

---

## 3. Żelazna Zasada Kompatybilności Ekosystemu (The Holy Trinity of Props)

> [!IMPORTANT]
> **Zasada Wstecznej i Pełnej Kompatybilności Ekosystemu:**
> Każdy nowo tworzony prop, pułapka, mechanizm, barykada czy element zniszczalny wprowadzany do świata gry **MUSI bezwzględnie implementować pełną trójcę ekosystemu**:
> 1. **Tożsamość Materiałowa (`IMaterialProviderInterface`):** Określa fizyczną naturę obiektu (`Stone`, `Wood`, `Metal`, `Glass`, `Flesh`). Determinuje dźwięki uderzeń, opór kinetyczny i podatność na statusy.
> 2. **Wytrzymałość i Życie (`UDamageableComponent`):** Punkty wytrzymałości (`Durability`/`Health`), obsługa zniszczenia, próg pęknięcia od prędkości uderzenia (`MinImpactDamageSpeed`) oraz autorytatywna replikacja stanu po sieci.
> 3. **Reaktywność Żywiołowa (`UStatusEffectComponent`):** Zdolność do przyjmowania powłok cieczy (`Wet`, `Oiled`), wchodzenia w reakcje alchemiczne (`Steam_Extinguish`, `Oil_Ignition`, `Conductive_Shock`) oraz otrzymywania okresowych obrażeń DoT.
>
> Żaden obiekt fizyczny w lochu nie może być "pustym Static Meshem". Wszystko w świecie gry musi podlegać prawom kinetyki, zniszczenia i magii żywiołów.

---

## 4. Katalog i Taksonomia Blueprintów Propów (Production Blueprints)

Koniec z szarymi bryłami testowymi. Wszystkie obiekty w Theme Parku otrzymują docelowe Static Meshe, materiały fizyczne i parametry rozgrywki.

### 4.1. Struktury Lochu (`ADungeonStructureBase`)
Bazowe moduły budowlane lochu.
* **`BP_Wall_Solid_Stone`**: Niezniszczalna ściana kamienna (`Stone`), blokująca ruch i wzrok.
* **`BP_Wall_Breachable_Stone`**: Spękana ściana kamienna o `Durability = 80.0`. Wymaga silnego uderzenia kinetycznego (ciężki głaz, wybuch) do skruszenia i otwarcia tajnego przejścia (Punch-Through).
* **`BP_Barricade_Wood`**: Drewniana barykada zagradzająca przejście (`Wood`, `Durability = 40.0`). Może zostać spalona ogniem (`Burning`) lub rozbita uderzeniem.
* **`BP_Floor_Dungeon_Stone`**: Płyta podłogowa kamienna.
* **`BP_Floor_Cracked_Fallthrough`**: Spękana posadzka (`Durability = 25.0`), która pęka pod ciężarem spadającego głazu lub wybuchem, odsłaniając pułapkę/niższy poziom.

---

### 4.2. Rekwizyty Interaktywne (`AInteractivePropBase`)
Fizyczne obiekty do manipulacji, rzucania, obrony i torowania drogi.

#### Kategoria: Kamienie i Głazy (Tożsamość: `Stone`)
* **`BP_Stone_Small` (Kamyk / Pocisk)**:
  * Masa: **`8 – 15 kg`** (można podnieść i rzucić `E` / LPM).
  * Zastosowanie: Amunicja dla gracza, pociski pułapek ściennych.
* **`BP_Stone_Medium` (Głaz zaporowy)**:
  * Masa: **`60 – 85 kg`** (nie do uniesienia rękami $> 35\text{ kg}$, ale **możliwy do przepchnięcia** ciałem i tarczą $\le 100\text{ kg}$).
  * Zastosowanie: Ruchome osłony, obiekty do zapychania korytarzy i dociskania płyt naciskowych.
* **`BP_Stone_HeavyBoulder` (Wielki Głaz Niszczyciel)**:
  * Masa: **`250 – 400 kg`** (niewzruszony dla rąk gracza $> 100\text{ kg}$, nie drgnie przy obrocie kamery).
  * Zastosowanie: Może zostać zepchnięty ze skarpy lub wyrzucony wybuchem, niszcząc barykady i miażdżąc wszystko na swojej drodze.

#### Kategoria: Drewno i Osłony (Tożsamość: `Wood`)
* **`BP_PlankShield_Wood` (Prowizoryczna Tarcza / Deska z uchwytem)**:
  * Masa: **`18 – 24 kg`** (łatwa do niesienia).
  * Wytrzymałość: `Durability = 50.0`.
  * Rola w walce: Gracz trzyma ją przed sobą metodą Kinematic Sweep. Blokuje lecące strzały, bełty i kamienie. Każde uderzenie pochłania energię i zużywa durability deski, chroniąc zdrowie gracza.
* **`BP_Crate_Small` (Mała Skrzynka)**:
  * Masa: **`15 kg`**, `Durability = 20.0`. Zwykły rekwizyt do rzucania i niszczenia.
* **`BP_Crate_Heavy` (Ciężka Skrzynia Magazynowa)**:
  * Masa: **`60 kg`**, `Durability = 60.0`. Może być przepychana przez gracza tarczą.
* **`BP_Barrel_Standard_Wood` (Zwykła Beczka Drewniana)**:
  * Masa: **`30 kg`**, czysty rekwizyt fizyczny (nie-wybuchowy).

#### Kategoria: Ceramika i Szkło (Tożsamość: `Glass` / Clay)
* **`BP_Pottery_Urn` / `BP_Vase_Clay` (Wazony i Amfory)**:
  * Masa: **`6 – 12 kg`**, `Durability = 1.0` (kruche).
  * Rozbijają się przy uderzeniu o podłogę z prędkością $> 150\text{ cm/s}$, generując satysfakcjonujący brzęk skorup.

---

### 4.3. Magiczne Kule Żywiołów (Alchemical Glass Orbs) – Ewolucja `VolatileProp`

> **Kluczowa zmiana koncepcyjna (Decyzja Projektowa):** 
> Usuwamy archaiczny motyw "beczek ze statusem". Efekty obszarowe nakładane na odległość przypominają alchemiczne bomby wiedźmińskie lub zakręcone w szkle anomalie.
> Wszystkie statusy żywiołowe zostają uwięzione w **Magicznych Szklanych Kulach (`Glass`, `Durability = 1.0`)**. Przy uderzeniu szkło pęka, a uwięziona energia rozprzestrzenia się falą uderzeniową.

* **`BP_GlassOrb_Fire` (Kula Ognia / Igni Bomb)**:
  * Materiał: `Glass`, Masa: **`5 kg`**.
  * Efekt przy rozbiciu: Wybuch termiczny, radialne obrażenia kinetyczne + nałożenie statusu `Burning (10s)`.
* **`BP_GlassOrb_Water` (Kula Głębin / Wodna Bomba)**:
  * Materiał: `Glass`, Masa: **`5 kg`**.
  * Efekt przy rozbiciu: Wodna fala uderzeniowa, nałożenie statusu `Wet (25s)`, natychmiastowe gaszenie ognia (`Steam_Extinguish`).
* **`BP_GlassOrb_Oil` (Kula Nafty / Alchemiczny Olej)**:
  * Materiał: `Glass`, Masa: **`5 kg`**.
  * Efekt przy rozbiciu: Rozbryzg lepkiego oleju, nałożenie statusu `Oiled (25s)`. Gotowa do podpalenia i gigantycznej reakcji łańcuchowej `Oil_Ignition`.
* **`BP_GlassOrb_Lightning` (Kula Burzy / Elektro-Granat)**:
  * Materiał: `Glass`, Masa: **`5 kg`**.
  * Efekt przy rozbiciu: Wyładowanie elektryczne, nałożenie `Electrified (10s)`, przewodzone natychmiast przez mokre cele (`Conductive_Shock`).
* **`BP_PowderKeg_Explosive` (Klasyczna Prochowa Beczka Wybuchowa)**:
  * Pozostaje jako potężna, ciężka beczka prochowa (**`75 kg`**, `Wood`). Potrzebuje ognia lub silnego uderzenia do detonacji o wielkiej sile kinetycznej (`Impulse = 180 000`).

---

## 5. Nowe Mechanizmy i Urządzenia Środowiskowe (Nowe Klasy C++)

Aby Theme Park tętnił fizyką i dynamiką, stworzymy 3 wyspecjalizowane, modularne mechanizmy w C++:

### 5.1. Taran Ścienny / Tłok Kamienny (`APistonTrap`)
Wysuwany z ogromną siłą kamienny blok służący jako śmiercionośna pułapka lub winda mechaniczna.
* **Kluczowe właściwości (Exposed to Blueprint):**
  * `PushDirection` (Wektor: w bok, w górę, w dół).
  * `StrokeDistance` (np. 300 cm wysunięcia).
  * `ExtendSpeed` / `RetractSpeed` (Błyskawiczne uderzenie 1200 cm/s, powolny powrót).
  * `RetractDelay` (Czas oczekiwania przed powrotem).
  * `bIsContinuousLoop` (Ciągłe uderzanie w pętli lub jednorazowy wystrzał po aktywacji przełącznikiem).
* **Trzy Presety:**
  1. **Taran Boczny (Wall Ram):** Wypycha gracza lub głazy z półki skalnej w przepaść.
  2. **Katapulta Podłogowa (Jump Pad Piston):** Wyrzuca gracza i prop pionowo w górę na wyższą kondygnację.
  3. **Zgniatacz Sufitowy (Ceiling Crusher):** Opada z impetem na ziemię, miażdżąc barykady lub nieuważnych graczy.

### 5.2. Miotacz Głazów / Pułapka Ścienna (`AProjectileLauncherTrap`)
Ścienna paszcza kamienna lub mechaniczna wyrzutnia kamieni/pocisków.
* **Kluczowe właściwości:**
  * `ProjectileClass`: Klasa spawanego aktora (np. `BP_Stone_Small` lub magiczna kula).
  * `LaunchSpeed`: Prędkość wylotowa (np. $1400\text{ cm/s}$).
  * `FireRate`: Częstotliwość strzałów w pętli.
  * `bTriggeredBySwitch`: Czy czeka na impuls z płyty naciskowej / wajchy.
* **Rola w Theme Parku:** Idealny poligon dla **testowania trzymanej tarczy (`BP_PlankShield_Wood`)**. Gracz idzie korytarzem pod prąd lecących kamieni, zasłaniając się deską i sprawdzając kąty osłony oraz utratę durability.

### 5.3. Fizyczna Płyta Naciskowa (`APressurePlateProp`)
Płyta podłogowa reagująca na **rzeczywistą sumaryczną masę** ciał znajdujących się na niej.
* **Kluczowe właściwości:**
  * `RequiredMass`: Minimalna masa do aktywacji (np. **`50.0 kg`**).
  * `TargetMechanisms`: Tablica aktorów do powiadomienia (`TArray<TScriptInterface<IInteractableInterface>>`).
* **Zasada działania:**
  * Jeśli wejdzie gracz ($\approx 80\text{ kg}$) $\rightarrow$ płyta zapada się ze szczękiem i otwiera wrota.
  * Jeśli gracz zejdzie $\rightarrow$ wrota się zamykają.
  * Aby utrzymać wrota otwarte, gracz musi przyciągnąć i postawić na płycie skrzynię `BP_Crate_Heavy` ($60\text{ kg}$) lub głaz `BP_Stone_Medium` ($70\text{ kg}$).

---

## 6. Układ Przestrzenny Poziomu Theme Park (Layout 4 Komnat)

```text
       [START / ZBROJOWNIA]
                │
                ▼
   ┌─────────────────────────┐
   │  KOMNATA 1: KINETYKA    │◄──── Korytarz Miotacza Głazów
   │  - Stojak z tarczami    │      (Test osłony tarczy pod ostrzałem)
   │  - Test pchania (60kg)  │
   │  - Taran boczny (Piston)│
   └────────────┬────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │  KOMNATA 2: ALCHEMIA    │
   │  - Magiczne Kule Szkła  │◄──── Baseniki i strefy rozlewisk
   │  - Łańcuchy: Woda->Ogień│      (Steam_Extinguish, Oil_Ignition)
   │  - Przewodzenie prądu   │
   └────────────┬────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │  KOMNATA 3: NISZCZENIE  │
   │  - Skarpa z głazem 300kg│◄──── Zepchnięcie głazu na spękaną podłogę
   │  - Barykada drewniana   │      i wyłom barykady (Punch-Through)
   │  - Wybuchowe wrota      │
   └────────────┬────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │  KOMNATA 4: ŚWIĄTYNIA   │
   │  - Płyta naciskowa 50kg │◄──── Zagadka fizyczna z dociążeniem głazem,
   │  - Dźwignie i wrota     │      krata podnoszona, finalny portal
   │  - Prefab dla generatora│
   └─────────────────────────┘
```

---

## 7. Rekomendacja Narzędzi i Assetów (Zasoby Zewnętrzne)

Aby uzyskać natychmiastowy, mroczny klimat dungeon crawlera bez modelowania 3D, skorzystamy z gotowych, darmowych pakietów od Epic Games:
1. **Epic Games: Infinity Blade (Adversaries, Props, Grass Lands)** – darmowe na Epic Marketplace / Fab:
   - Gotowe Static Meshe średniowiecznych desek, tarcz, skrzyń, amfor i kamieni.
2. **Quixel Megascans / Fab (Medieval Dungeon & Rough Stone)**:
   - Fotorealistyczne materiały i siatki spękanych skał oraz ciosanego kamienia.
3. **Materiały w Projekcie (`M_ColorBase` i Instancje)**:
   - Skonfigurujemy instancje materiałów z emisyjnym blaskiem (Glow) dla Magicznych Kul Szklanych (Niebieski dla Wody, Czerwony dla Ognia, Żółty dla Pioruna, Czarny/Brązowy z połyskiem dla Oleju).

---

## 8. Kolejność Realizacji (Harmonogram Wdrożenia)

1. **Etap 1: Nowe Narzędzia Mechaniczne w C++:**
   - Implementacja `APistonTrap` (taran ścienny/podłogowy).
   - Implementacja `AProjectileLauncherTrap` (miotacz kamieni).
   - Implementacja `APressurePlateProp` (płyta naciskowa reagująca na masę).
2. **Etap 2: Produkcyjne Blueprinty:**
   - Magiczne Kule Szklane (`BP_GlassOrb_Fire/Water/Oil/Lightning`).
   - Tarcza drewniana (`BP_PlankShield_Wood`) i głazy (`BP_Stone_Small/Medium/Heavy`).
   - Barykady i spękane ściany.
3. **Etap 3: Budowa i Złożenie Theme Parku:**
   - Zestawienie komnat z modułów `400x400 cm`.
   - Rozstawienie zagadek fizycznych i poligonu kinetycznego.
   - Weryfikacja rozgrywki w PIE (Listen Server + 2 graczy).

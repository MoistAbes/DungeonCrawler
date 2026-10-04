# Specyfikacja Techniczna i Projektowa: System Stref Statusów (Status Zone System)

Dokument stanowi **kompletną kartę projektowo-architektoniczną** dla modułu stref (`Status Zones`) oraz siatki powierzchniowej (`Surface Grid`) w projekcie *Dungeon Crawler Co-op* (Unreal Engine 5.8 C++, `MYPROJECT_API`).

System realizuje dwa zintegrowane wektory dostarczania efektów żywiołowych do świata:
1. **Powierzchniowy (Surface Grid)** — rzadka siatka komórek w `UDungeonSurfaceSubsystem` (WorldSubsystem), przypięta do geometrii lochu (posadzki, ściany, sufity).
2. **Wolumetryczny (Volumetric Zone)** — trójwymiarowa strefa przestrzenna `AVolumetricStatusZone` (sfera lub chmura zawieszona w powietrzu).

Dostarczaniem efektów zarządza zunifikowana fabryka `UStatusZoneLibrary`, a cała chemia żywiołowa (reakcje, kompatybilność materiałowa, rozprzestrzenianie, wygaszanie) jest scentralizowana w `UElementalReactionRules` jako **Jedynym Źródle Prawdy (Single Source of Truth)**.

Model sieciowy: **Server-Authoritative**, **Zero-Bandwidth Timers** (replikacja `ServerEndTime`), **Net Dormancy** (`DORM_DormantAll`).

---

## 1. Wizja i Cele Systemu

1. **Uniwersalność Domenowa:**
   Strefa reprezentuje dowolny obszar oddziaływania na rozgrywkę w lochu:
   - **Żywioły i Ciecze:** Ogień (`Burning`), rozlana woda (`Wet`), plama oleju (`Oiled`), prąd (`Electrified`).
   - **Zjawiska Środowiskowe:** Trująca chmura, dym, mgła parowa, strefy ciszy (strefy wolumetryczne).
   - **Mechaniki Rozgrywki:** Obrażenia ciągłe (DoT), odrzut kinetyczny (Knockback), reakcje łańcuchowe i propagacja.

2. **Dwa Zintegrowane Wektory Dostarczania:**
   - **Surface Grid (Siatka Powierzchniowa):** Cienka, dyskretna warstwa przypięta do geometrii lochu (posadzki, ściany, sufity). Realizowana przez `UDungeonSurfaceSubsystem` jako rzadka mapa `TMap<FSurfaceCellCoord, FSurfaceCellData>`, z pełną świadomością strony fundamentu (6 kierunków w 3D) oraz wielostatusowością komórek.
   - **Volumetric Zone (Strefa Wolumetryczna):** Trójwymiarowa bryła zawieszona w przestrzeni lub poruszająca się (np. aura postaci, pocisk, wędrująca chmura gazu). Realizowana przez aktora `AVolumetricStatusZone`.

3. **Ciągła Dwukierunkowa Integracja (Surface $\leftrightarrow$ Volumetric):**
   - Strefy wolumetryczne w `BeginPlay` rejestrują się w `UDungeonSurfaceSubsystem`, a w `EndPlay` wyrejestrowują.
   - Wylanie płynu na podłoże pod wiszącą strefą (np. rzucenie oleju w chmurę prądu) wywołuje natychmiastową reakcję w tej samej klatce (`bCheckOverlappingZones`).
   - Pętla serwera `UDungeonSurfaceSubsystem::ProcessGridTick` (0.25 s) nieustannie ewaluuje aktywne strefy przestrzenne z komórkami posadzki pod nimi, obsługując ruchome strefy i aury.

4. **Tryby Dostarczania Efektów (`EVolatileZoneSpawnMode`):**
   W obiektach wybuchowych i miotanych (`AVolatileProp`) stosowany jest dedykowany tryb dostarczenia:
   - **RadialBurst** — natychmiastowy wybuch 3D z Line-of-Sight (obrażenia, odrzut kinetyczny) oraz wszechkierunkowa projekcja na powierzchnie lochu.
   - **PointImpact** — uderzenie punktowe w pojedynczą powierzchnię lub cel (np. rzucona butelka z cieczą).
   - **VolumetricZone** — trwała strefa przestrzenna zawieszona w powietrzu (np. chmura gazu, burza elektryczna).

5. **Architektura Co-op i Optymalizacja Sieciowa (1–6 Graczy):**
   - **Zero-Bandwidth Timers:** Replikacja wyłącznie `ServerEndTime`. Klienci lokalnie odliczają upływ czasu.
   - **Server-Authoritative Gameplay:** Aplikacja statusów, obrażeń i reakcji zachodzi w 100% na serwerze.
   - **Net Dormancy:** Strefy wolumetryczne po utworzeniu natychmiast przechodzą w `DORM_DormantAll`.

6. **Centralizacja Praw Żywiołów (Single Source of Truth):**
   Wszelkie reguły chemiczne, kompatybilność fizyczna materiałów, wypieranie płynów i czyszczenie osieroconych statusów są zaimplementowane w `UElementalReactionRules::CalculateElementalTransition`. Zarówno komponenty aktorów (`UStatusEffectComponent`), jak i komórki posadzki (`UDungeonSurfaceSubsystem`) korzystają z tego samego silnika.

---

## 2. Architektura Klas C++

```mermaid
classDiagram
    class AStatusZoneBase {
        <<Abstract Base>>
        #USphereComponent* ZoneCollision
        #FZoneEffectConfig EffectConfig
        #float Radius
        #float ServerEndTime
        #float ZoneCreationTime
        #float ZoneTickInterval
        #TWeakObjectPtr~AActor~ ZoneInstigator
        +InitializeZoneBase(Config, Radius, Duration, Instigator)
        +ApplyElementalHit(IncomingStatus, InstantDamage, Instigator)
        +MergeWithZone(Duration, RadiusGrowthMultiplier, MaxRadiusCap)
        +GetZoneInstigator() AActor*
        +IsPointWithinZoneGeometry(Point) bool
        #ProcessActiveOverlaps()
        #IsActorEligibleForZoneEffect(Actor, Comp) bool
        #IsActorWithinZoneGeometry(Bounds) bool
    }

    class AVolumetricStatusZone {
        +InitializeVolumetricZone(Config, Radius, Duration, Instigator)
        #IsActorWithinZoneGeometry(Bounds) bool override
        #DrawDebugVisuals() override
    }

    class UDungeonSurfaceSubsystem {
        <<WorldSubsystem Facade>>
        -TUniquePtr~FStaticSurfaceGridManager~ StaticGridManager
        -TUniquePtr~FDynamicSurfaceGridManager~ DynamicGridManager
        -TArray~TWeakObjectPtr~AStatusZoneBase~~ RegisteredStatusZones
        -TArray~TWeakObjectPtr~UStatusEffectComponent~~ RegisteredStatusComponents
        #float CellSize
        #float SubsystemTickInterval
        +IsValidSurfaceTarget(Actor) bool
        +ApplyStatusFromHit(Hit, Radius, Status, Duration, Instigator, Tier) int32
        +ApplyStatusToSurface(Location, Normal, Radius, Status, Duration, Instigator, Tier) int32
        +ApplyStatusToCell(Coord, Status, Duration, Instigator, Material, SurfaceActor, Tier) bool
        +ApplyElementalBurst(Origin, Radius, Status, Duration, Instigator, Tier) int32
        +ClearCellsInBounds(BoundingBox) int32
        +RegisterStatusZone(Zone)
        +UnregisterStatusZone(Zone)
        +RegisterStatusComponent(Comp)
        +UnregisterStatusComponent(Comp)
        #ProcessGridTick()
    }

    class FStaticSurfaceGridManager {
        -TMap~FSurfaceCellCoord, FSurfaceCellData~ ActiveCells
        +TickStaticGrid(CurrentTime, SafeCellSize, World, ...)
        +ApplyStatusToCell(...) bool
        +ProcessSolidFuelCombustion(...)
    }

    class FDynamicSurfaceGridManager {
        -TMap~TWeakObjectPtr~AActor~, FDynamicActorSurfaceGrid~ DynamicGrids
        +RegisterDynamicActorSurface(Actor, TransformComp)
        +UnregisterDynamicActorSurface(Actor)
        +TickDynamicGrids(CurrentTime, SafeCellSize, World, ...)
        +ApplyStatusToDynamicCell(...) bool
    }

    class FSurfaceGridPropagationUtils {
        +PropagateElementalSpreads(...)
        +PropagateConductionNetworks(...)
    }

    class FSurfaceGridDamageUtils {
        +ProcessSurfaceStructuralDamage(...)
    }

    class UStatusZoneLibrary {
        <<BlueprintFunctionLibrary>>
        +SpawnVolumetricZone(...) AVolumetricStatusZone*
        +ApplyRadialBurst(...)
        +ApplyPointImpact(...) bool
        +ApplyPointHit(...) bool
    }

    class UElementalReactionRules {
        <<BlueprintFunctionLibrary>>
        +GetEffectConfig(Status) FStatusEffectConfig&
        +IsLiquidStatus(Status) bool
        +CanMaterialReceiveStatus(Material, IncomingStatus, Active) bool
        +CanMaterialSustainStatus(Material, Status, Active) bool
        +EvaluateReaction(Incoming, Active) FElementalReactionResult
        +CalculateElementalTransition(Material, Incoming, Duration, Active) FElementalTransitionPlan
        +CanSpreadToNeighbor(Source, Target, OutReaction) bool
        +GetReactionPriority(Status) int32
        +SortByReactionPriority(InOutStatuses)
    }

    class UStatusEffectComponent {
        <<ActorComponent>>
        +ApplyStatus(NewStatus, Duration, Instigator) bool
        +RemoveStatus(Status) bool
        +GetActiveStatuses() TArray~EStatusEffectType~
    }

    AStatusZoneBase <|-- AVolumetricStatusZone
    AStatusZoneBase ..> UDungeonSurfaceSubsystem : Rejestracja w BeginPlay / EndPlay
    UDungeonSurfaceSubsystem o-- AStatusZoneBase : Ciągła ewaluacja stref z komórkami
    UDungeonSurfaceSubsystem *-- FStaticSurfaceGridManager : Zarządzanie siatką świata
    UDungeonSurfaceSubsystem *-- FDynamicSurfaceGridManager : Zarządzanie siatkami aktorów
    UDungeonSurfaceSubsystem ..> FSurfaceGridPropagationUtils : Propagacja komórkowa
    UDungeonSurfaceSubsystem ..> FSurfaceGridDamageUtils : Agregacja obrażeń struktur
    UStatusZoneLibrary ..> AVolumetricStatusZone : Fabryka
    UStatusZoneLibrary ..> UDungeonSurfaceSubsystem : Projekcja wybuchów i uderzeń
    UDungeonSurfaceSubsystem ..> UElementalReactionRules : CalculateElementalTransition
    UStatusEffectComponent ..> UElementalReactionRules : CalculateElementalTransition
```

### Podział Odpowiedzialności Klas:

- **`AStatusZoneBase` (`Source/MyProject/Environment/Zones/StatusZoneBase.h`):**
  Abstrakcyjny aktor bazowy dla stref przestrzennych. Zarządza autorytatywnym cyklem życia serwera, replikacją w modelu Zero-Bandwidth (`DORM_DormantAll`, `ServerEndTime`), kolizją (`USphereComponent ZoneCollision`), aplikacją statusów na wchodzących aktorów oraz rejestracją w `UDungeonSurfaceSubsystem`. Udostępnia polimorficzny test punktowy `IsPointWithinZoneGeometry(Point)`.

- **`AVolumetricStatusZone` (`Source/MyProject/Environment/Zones/Shapes/VolumetricStatusZone.h`):**
  Implementacja przestrzennej bryły sferycznej (chmury gazu, kule energii, mgła). Obsługuje scalanie nakładających się stref tego samego żywiołu (`MergeWithZone`) oraz rzutowanie geometrii na cele i podłoże.

- **`UDungeonSurfaceSubsystem` (`Source/MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h`):**
  Podsystem świata (`UWorldSubsystem`) i fasada orkiestrująca działanie siatki powierzchniowej w lochu. Deleguje zadania do dedykowanych zarządców i bibliotek:
  - Przechowuje i koordynuje `FStaticSurfaceGridManager` oraz `FDynamicSurfaceGridManager`.
  - Prowadzi rejestr stref przestrzennych `RegisteredStatusZones` i komponentów aktorów `RegisteredStatusComponents`.
  - Pętla serwera `ProcessGridTick` (0.25 s) wywołuje cykle zarządców, propagację (`FSurfaceGridPropagationUtils`), interakcję aktorów (`SurfaceActorInteractionUtils`) oraz agregację obrażeń (`FSurfaceGridDamageUtils`).
  - Udostępnia publiczne, atomowe API: `ApplyStatusFromHit`, `ApplyStatusToSurface`, `ApplyStatusToCell`, `ApplyElementalBurst`, `ClearCellsInBounds`.

- **`FStaticSurfaceGridManager` (`Source/MyProject/Environment/Zones/Managers/StaticSurfaceGridManager.h`):**
  Zarządca statycznej siatki świata: rzadka mapa `ActiveCells`, cykl życia statusów (wygaszanie, DoT), spalanie paliw stałych (`ProcessSolidFuelCombustion`) oraz aplikacja komórkowa na architekturze.

- **`FDynamicSurfaceGridManager` (`Source/MyProject/Environment/Zones/Managers/DynamicSurfaceGridManager.h`):**
  Zarządca lokalnych siatek powierzchniowych przypiętych do ruchomych obiektów i mechanizmów lochu (bramy, platformy, skrzynie). Przelicza pozycje w lokalnych układach współrzędnych za pomocą transformacji sztywnej `GetDynamicRigidTransform`.

- **`FSurfaceGridPropagationUtils` (`Source/MyProject/Environment/Zones/Utilities/SurfaceGridPropagationUtils.h`):**
  Silnik rozprzestrzeniania żywiołów: propagacja komórkowa w czasie (`PropagateElementalSpreads`), łańcuchowe sieci przewodzenia elektrycznego (`PropagateConductionNetworks`) oraz synchronizacja punktów styku między siatką statyczną i ruchomymi aktorami.

- **`FSurfaceGridDamageUtils` (`Source/MyProject/Environment/Zones/Utilities/SurfaceGridDamageUtils.h`):**
  Pipeline obrażeń środowiskowych: agregacja DoT komórek per aktor i typ obrażeń, ograniczenie `MaxStructuralDPS` i przekazywanie obrażeń do `UDamageableComponent`.

- **`SurfaceGridGeometryUtils` (`Source/MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h`):**
  Narzędzia topologii fizycznej i próbkowania przestrzennego (`ProbeSurfaceAt`, `FindSpreadCandidates` z 4-wariantowym rozwiązywaniem narożników 90° `GetCornerCandidateCoords`, kanonizacją woksela `FromWorldLocation`, weryfikacją LoS i separacją strukturalną `CornerActor != SourceActor`).

- **`SurfaceGridProjectionUtils` (`Source/MyProject/Environment/Zones/Utilities/SurfaceGridProjectionUtils.h`):**
  Narzędzia rzutowania wybuchów 3D, próbkowania dysków powierzchniowych, testów krawędzi (drop-off line traces) i weryfikacji linii wzroku (Line-of-Sight).

- **`SurfaceActorInteractionUtils` (`Source/MyProject/Environment/Zones/Utilities/SurfaceActorInteractionUtils.h`):**
  Narzędzia dwukierunkowej wymiany żywiołów posadzka <-> aktor (Faza A: transfer z aktora na komórkę; Faza B: transfer z komórki na aktora).

- **`USurfaceCellTransitionUtils` (`Source/MyProject/Environment/Zones/Utilities/SurfaceCellTransitionUtils.h`):**
  Adapter aplikujący reguły reakcji żywiołowych (`CalculateCellTransition`) bezpośrednio na strukturę `FSurfaceCellData` oraz zarządzający stanem wizualnym podłoża (np. płonącą posadzką).

- **`UStatusZoneLibrary` (`Source/MyProject/Environment/Zones/Utilities/StatusZoneLibrary.h`):**
  Statyczna fabryka operacji obszarowych: `SpawnVolumetricZone`, `ApplyRadialBurst`, `ApplyPointImpact`, `ApplyPointHit`.

- **`UElementalReactionRules` (`Source/MyProject/Environment/Elements/Utilities/ElementalReactionRules.h`):**
  Główny silnik praw żywiołów świata gry. Udostępnia funkcję `CalculateElementalTransition`, która stanowi unikalne źródło prawdy dla wszystkich zmian chemiczno-fizycznych w projekcie.

---

## 3. Podsystem Siatki Powierzchniowej (Surface Grid)

`UDungeonSurfaceSubsystem` przechowuje wyłącznie komórki posiadające aktywny status (`ActiveCells`). Puste fragmenty geometrii lochu nie generują żadnego narzutu pamięciowego.

### 3.1. Typy Danych (`SurfaceGridTypes.h`)
- `ESurfaceFaceDirection` — 6 kierunków normalnej fundamentu: `Up` (posadzka), `Down` (sufit), `North`, `South`, `East`, `West` (ściany pionowe). Zapewnia niezależność przeciwnych stron ścian.
- `FSurfaceCellCoord` — trójwymiarowy klucz siatki: `(X, Y, Z, Face)`. Posiada metody mapowania `FromWorldLocation` (z 2 cm marginesem w głąb kafelka), `ToWorldLocation`, `GetCoplanarNeighbors`, `GetDirectionalSpreadPaths`, `GetCornerCandidateCoords` (wyznacza 4 warianty brzegowe wokseli na krawędziach 90°) oraz `GetAdjacentNeighbors`.
- `FSurfaceCellStatusEntry` — pojedynczy status aktywny na komórce: `Status`, `Tier`, `ServerEndTime`, `Instigator`.
- `FSurfaceCellData` — kontener komórki: lista statusów `ActiveStatuses` (`TArray<FSurfaceCellStatusEntry, TInlineAllocator<2>>`), tożsamość materiału `SurfaceMaterial` (`EPhysicalMaterialType`), wskaźnik na aktora `SurfaceActor`, metody pomocnicze (`HasStatus`, `FindStatus`, `RemoveStatus`, `GetDominantStatus`).
- `FDynamicActorSurfaceGrid` — lokalna rzadka siatka wokseli przypięta do komponentu `USceneComponent` aktora ruchomego (`OwnerActor`, `LocalCells`). Przeliczana do świata za pomocą transformacji sztywnej `GetDynamicRigidTransform`.

### 3.2. Konfiguracja Subsystemu

| Parametr | Domyślnie | Opis |
| :--- | :--- | :--- |
| `CellSize` | 50.0 cm | Dyskretny rozmiar boku pojedynczej komórki siatki. |
| `SubsystemTickInterval` | 0.25 s | Okres pętli serwera: wygaszanie, propagacja, interakcja z aktorami i strefami. |
| `bDrawDebugGrid` | true | Serwerowe renderowanie wizualizacji aktywnych komórek w trybach debugowych. |

### 3.3. Atomowy Punkt Styku: `ApplyStatusToCell`

Wszelkie zmiany stanu komórki (niezależnie czy wywołane wybuchem, pociskiem, propagacją, strefą czy wejściem postaci) przechodzą przez pojedynczą funkcję `ApplyStatusToCell`:
1. Pobiera obecną tożsamość materiałową podłoża (`ExplicitMaterial` lub trace w głąb architektury).
2. Wywołuje `USurfaceCellTransitionUtils::CalculateCellTransition` (która deleguje reguły chemiczne do `UElementalReactionRules::CalculateElementalTransition`).
3. Usuwa statusy wygaszone/skonsumowane/wyparte (`Plan.StatusesToRemove`).
4. Dodaje lub odświeża statusy wynikowe (`Plan.StatusesToApply`).
5. **Natychmiastowa ewaluacja stref (`bCheckOverlappingZones`):** Jeśli komórka znajduje się pod zarejestrowaną strefą wolumetryczną (np. świeży olej wylany pod chmurę prądu), funkcja natychmiast aplikuje żywioł strefy z flagą `bCheckOverlappingZones = false` (zabezpieczenie przed pętlą rekurencyjną).
6. Rozgłasza delegat `OnSurfaceCellChanged` dla efektów wizualnych i dźwiękowych.

### 3.4. Pipeline Aplikacji Statusów na Siatkę (PointImpact vs RadialBurst)

Zarządzanie aplikacją statusów w siatce lochu jest ściśle rozdzielone na dedykowane metody w oparciu o geometrię zdarzenia i perspektywę Line-of-Sight (LoS):

1. **`ApplyStatusFromHit` (Hit Resolver):**
   - Publiczne API. Rozwiązuje uderzenia pociskami, rzutami i obiektami fizycznymi.
   - Weryfikuje cel za pomocą `IsValidSurfaceTarget(HitActor)` (odrzuca postacie i dynamiczne rekwizyty posiadające własne komponenty statusów).
   - Wyznacza normalną uderzenia i przekazuje wykonanie do `ApplyStatusToSurface`.

2. **`ApplyStatusToSurface` (PointImpact — Pojedyncza Powierzchnia 2D):**
   - Tworzy dysk statusu na jednej konkretnej ścianie, suficie lub posadzce (np. rozbicie flakonu).
   - Wyznacza komórki w promieniu na płaszczyźnie stycznej (`TangentU`, `TangentV`).
   - Weryfikuje LoS wzdłuż powierzchni za pomocą `SurfaceGridGeometryUtils::HasSurfaceLineOfSight`.
   - Korzysta z `LineTraceMultiByChannel` (`ECC_Visibility`) na wysokości 10 cm nad płaszczyzną, wykrywając przeszkody poprzeczne (filary, prostopadłe ściany, skrzynie) bez ryzyka zablokowania na drobnych nierównościach podłoża.

3. **`ApplyElementalBurst` & `ApplyStatusInArea` (RadialBurst — Eksplozja Przestrzenna 3D):**
   - Wszechkierunkowy wybuch 3D (detonacja beczki, bomby lub początkowa projekcja strefy `AVolumetricStatusZone`).
   - **Krok 1 (Ewaluacja Istniejących Komórek):** Odpytuje aktywne `ActiveCells` w promieniu sferycznym pod kątem bezpośredniej widoczności 3D (`HasDirectBurstLineOfSight`).
   - **Krok 2 (18 Promieni Skanujących):** Wystrzeliwuje promienie w 18 kierunkach 3D (`GetBurstScanDirections`). Dla każdego trafionego fundamentu oblicza promień rozbryzgu i wywołuje `ApplyStatusInArea`.
   - **Krok 3 (`ApplyStatusInArea`):** Iteruje po komórkach trafionego fragmentu. Wykonuje Drop-Off Test (`CheckSurfacePresenceAt`) oraz ścisłą weryfikację 3D LoS:
     - `SurfaceGridGeometryUtils::HasDirectBurstLineOfSight` z użyciem `LineTraceMultiByChannel`.
     - **Backface Culling (`Dot <= 0`):** Odrzuca powierzchnie odwrócone tyłem do fali wybuchu.
     - **Weryfikacja Przeszkód 3D:** Sprawdza całą trasę promienia. Pomija płaskie muśnięcia tej samej płyty podłogowej (`PlaneDist < 8 cm`, `Dot > 0.7`), ale bezwzględnie blokuje wybuch przy napotkaniu ścian, filarów i rekwizytów.
     - Przekazuje referencję `TSet<FSurfaceCellCoord>& ProcessedCoords`, zabezpieczając przed wielokrotnym nakładaniem statusu na tę samą komórkę w jednym wybuchu.

### 3.5. Niezmienniki Siatek Dynamicznych i Propagacji Przewodzenia

W celu zagwarantowania stabilności wydajnościowej oraz wyeliminowania duplikacji komórek na ruchomych obiektach (np. `BP_DungeonGate_Portcullis`) wprowadzono ścisłe reguły:

1. **Niezmiennik Braku Alokacji w Siatce Dynamicznej (Dynamic Conduction Invariant):**
   - Propagacja przewodzenia elektrycznego (`PropagateConductionNetworks`) na obiektach ruchomych **nie może tworzyć nowych komórek w pustej przestrzeni**.
   - Prąd (`Electrified`) może rozprzestrzenić się na woksel w siatce dynamicznej (`FDynamicActorSurfaceGrid`) **wyłącznie wtedy**, gdy dana komórka została już wcześniej zainicjalizowana i jest aktywna na geometrii obiektu (np. pokryta wodą `Wet`).
   - Dwukierunkowa bariera styku:
     - Statyczna $\rightarrow$ Dynamiczna: promień przewodzenia aktywuje wyłącznie istniejące komórki lokalne (`LocalCells.Find(LocalCoord)`).
     - Dynamiczna $\rightarrow$ Statyczna: kontakt dynamiczny nie tworzy komórek statycznych znikąd w ścianach/posadzkach, lecz łączy się tylko z istniejącymi komórkami świata (`ActiveCells.Find(WorldCoord)`).
   - Rezultat: liczba komórek na ruchomym obiekcie jest sztywno ograniczona do jego fizycznej geometrii (np. dokładnie 42 komórki dla kraty bramy).

2. **Bufor Pomijania Ciągłego Odświeżania (Continuous Zone Skip Buffer):**
   - W `ApplyContinuousZoneToCells`: komórki (zarówno statyczne, jak i dynamiczne), których pozostały czas trwania przekracza próg `MinRemainingToSkip = 1.0f`, są całkowicie pomijane.
   - Zapobiega to ciągłemu resetowaniu czasów i generowaniu fałszywych zdarzeń modyfikacji co 0.25 s dla stabilnych stref.

3. **Warunkowe Uruchamianie BFS Przewodzenia (Selective Conduction BFS):**
   - W `ApplyElementalBurst` wprowadzono licznik `NewlyAddedCount`.
   - Procedura `PropagateConductionIfApplicable` w pętli strefy ciągłej jest wywoływana **wyłącznie wtedy, gdy `NewlyAddedCount > 0`** (do sieci przewodzenia faktycznie dołączyła nowa komórka).
   - Dopóki strefa podtrzymuje jedynie istniejące kałuże, kosztowny algorytm BFS śpi, redukując obciążenie Game Thread.

4. **Jednokierunkowy Transfer Prądu Aktor $\rightarrow$ Podłoże:**
   - W regułach żywiołowych (`ElementalReactionRules.cpp`) ustawiono `Electrified.bCanTransferFromActorToFloor = false`.
   - Prąd na aktorze (np. debuff obrażeń) nie przekazuje się z powrotem na posadzkę, co eliminuje nieskończoną pętlę wzajemnego ładowania się podłogi i postaci.

---

## 4. Zunifikowana Karta Efektu (`FZoneEffectConfig`)

```mermaid
classDiagram
    class FZoneEffectConfig {
        +EStatusEffectType AppliedStatus
        +float InstantDamage
        +float KnockbackForce
        +float ContinuousDamagePerSec
    }
```

Pola w strukturze `FZoneEffectConfig` (`ZoneTypes.h`):
- **AppliedStatus** — nakładany status żywiołowy (`Burning`, `Wet`, `Oiled`, `Electrified`, `None`).
- **InstantDamage** — jednorazowe obrażenia w momencie wybuchu lub trafienia.
- **KnockbackForce** — siła radialnego odrzutu fizycznego celów w linii wzroku (LoS).
- **ContinuousDamagePerSec** — ciągłe obrażenia zadawane celom przebywającym w strefie co sekundę.

---

## 5. Centralny Silnik Chemii Żywiołów (`UElementalReactionRules`)

Wszelkie decyzje o reakcjach chemicznych, kompatybilności materiałowej i wygaszaniu statusów podejmuje funkcja `CalculateElementalTransition`:

```cpp
FElementalTransitionPlan UElementalReactionRules::CalculateElementalTransition(
    EPhysicalMaterialType TargetMaterial,
    EStatusEffectType IncomingStatus,
    float IncomingDuration,
    const TArray<EStatusEffectType>& CurrentActiveStatuses);
```

### 5.1. Różnica Pomiędzy `CanMaterialReceiveStatus` a `CanMaterialSustainStatus`

W systemie obowiązuje ścisłe fizyczne rozróżnienie pomiędzy **przyjęciem nowego statusu** a **podtrzymaniem statusu już aktywnego**:

- **`CanMaterialReceiveStatus` (Walidacja Nowego Statusu):**
  Określa, czy materiał może zostać zainfekowany nowym żywiołem:
  - Płyny (`Wet`, `Oiled`) mogą pokryć dowolny materiał.
  - Ogień (`Burning`) wymaga materiału łatwopalnego (`bFlammable == true`, np. drewno, ciało) LUB obecności łatwopalnego nośnika na celu (`ActiveStatuses.Contains(Oiled)`). Suchy kamień **nie przyjmie** ognia od pochodni.
  - Prąd (`Electrified`) wymaga materiału przewodzącego (`bConductive == true`, np. metal, ciało) LUB obecności cieczy przewodzącej (`ActiveStatuses.Contains(Wet)`). Suchy kamień **nie przyjmie** ładunku elektrycznego.

- **`CanMaterialSustainStatus` (Podtrzymanie Statusu Aktywnego):**
  Określa, czy dany status może nadal istnieć na materiale po usunięciu innego statusu:
  - Ogień (`Burning`): jest procesem **samopodtrzymującego się spalania paliwa**. Gdy olej na kamieniu ulegnie zapłonowi, olej zostaje skonsumowany (`StatusesToRemove.Add(Oiled)`), ale powstały płomień podtrzymuje się na kamieniu przez pełen czas spalania (`ResultingDuration = 6.0s`). Ogień na kamieniu **nigdy nie jest traktowany jako osierocony** — gaśnie wyłącznie po upływie czasu lub po zalaniu wodą.
  - Prąd (`Electrified`): jest zjawiskiem **pasożytniczego przepływu ładunku**. Na izolatorach (suchy kamień) prąd nie ma paliwa i wymaga ciągłej obecności wody (`Wet`). Gdy woda odparuje lub zostanie wyparta, prąd na kamieniu natychmiast ulega rozproszeniu (jest osierocony).

### 5.2. Fazy Ewaluacji w `CalculateElementalTransition`

1. **Odświeżenie:** Jeśli `IncomingStatus` jest już obecny na celu, zwracany jest plan z odświeżonym czasem trwania.
2. **Ewaluacja Reakcji (`EvaluateReaction`):**
   - **Dedykowana Reakcja:** Sprawdzenie reguł z karty przychodzącego żywiołu (np. `Oil_Electric_Ignition`). Jeśli reakcja wygenerowała `ResultingStatus` (np. `Burning`), jest on dodawany do planu, o ile materiał może go podtrzymać (`CanMaterialSustainStatus`).
   - **Wypieranie Płynów (Liquid Displacement):** Każdy nowy płyn (`bIsLiquid`) wypiera obecny na powierzchni płyn (np. woda zmywa olej, olej wypiera wodę).
   - **Brak Reakcji:** Weryfikacja kompatybilności materiałowej przez `CanMaterialReceiveStatus`.
3. **Czyszczenie Osieroconych Statusów Pasożytniczych:**
   Uruchamiane **wyłącznie wtedy**, gdy w bieżącej transformacji faktycznie usunięto jakiś status nośnika (`!Plan.StatusesToRemove.IsEmpty()`). Weryfikuje pozostałe statusy za pomocą `CanMaterialSustainStatus`.

### 5.3. Matryca Reakcji Żywiołowych

| Istniejący Status | Przychodzący Status | Tag Reakcji (`ReactionTag`) | Skutek Fizyczno-Chemiczny |
| :--- | :--- | :--- | :--- |
| Olej (`Oiled`) | Ogień (`Burning`) | `Oil_Ignition` | Zapłon plamy oleju: olej staje się paliwem podtrzymującym płomień `[Oiled, Burning]`. Czas trwania ognia synchronizuje się z pozostałym czasem oleju (`bSyncWithCarrierDuration`), obrażenia, propagacja ognia na sąsiednie komórki. |
| Olej (`Oiled`) | Prąd (`Electrified`) | `Oil_Electric_Ignition` | Iskra elektryczna detonuje plamę oleju: zapłon paliwa `[Oiled, Burning]`, synchronizacja czasu z nośnikiem (`bSyncWithCarrierDuration`), obrażenia, propagacja ognia na sąsiednie komórki. |
| Ogień (`Burning`) | Woda (`Wet`) | `Steam_Extinguish` | Ugaszenie ognia: usunięcie `Burning`, odparowanie wody (para wodna), brak DoT. |
| Woda (`Wet`) | Prąd (`Electrified`) | `Conductive_Shock` | Przewodzenie: koegzystencja obu statusów `[Wet, Electrified]`, obrażenia szokowe, propagacja prądu po całej kałuży. Czas trwania prądu na izolatorach jest ograniczony do czasu obecności wody. |
| Płyn A (`Wet` / `Oiled`) | Płyn B (`Oiled` / `Wet`) | `Liquid_Displaced` | Wypieranie powłoki płynnej: nowy płyn bezwzględnie zmywa obecny płyn na powierzchni, zajmując jego miejsce (Liquid Mutual Exclusivity). |

### 5.4. Dynamiczna Synchronizacja Nośnika (Carrier Duration Synchronization)

System realizuje uniwersalny mechanizm nośników (`DoesStatusSyncWithCarrier`, `bSyncWithCarrierDuration`):
1. **Wektor Zależny $\rightarrow$ Nośnik (Inherit):**
   Gdy status zależny (np. `Burning` na kamieniu lub `Electrified` na kamieniu) jest nakładany na cel posiadający aktywny nośnik (`Oiled` lub `Wet`), jego czas trwania jest wyliczany funkcją `ComputeAdjustedDuration` i synchronizowany z pozostałym czasem paliwa/nośnika.
2. **Wektor Nośnik $\rightarrow$ Zależny (Extend / Forward Sync):**
   Gdy do celu posiadającego już aktywny status zależny (np. płonąca posadzka lub naelektryzowany wróg) zostanie dodany nowy nośnik (np. wylanie butli oleju 30s na palący się kamień lub oblanie wodą naelektryzowanego wroga), funkcje `SyncDependentsWithCarrier` (dla siatki) i `SyncDependentStatusesWithCarrier` (dla komponentu aktora) automatycznie wydłużają czas trwania statusów zależnych do nowego czasu życia nośnika.
3. **Zasada Wyłączności Cieczy (Liquid Mutual Exclusivity):**
   Zunifikowana w metodach `EnforceLiquidMutualExclusivity` / `DisplaceOtherLiquids`. Nałożenie cieczy usuwa wszelkie inne ciecze i wygasza osierocone przez nie ładunki.

### 5.5. Spalanie Paliw Stałych (Solid Fuel Combustion) i Destrukcja Architektur

System realizuje model fizycznego spalania palnych materiałów konstrukcyjnych lochu (`Wood`):

1. **Paliwo Samoistne (`bSelfSustainingFuel`):**
   Materiały z cechą `bSelfSustainingFuel = true` traktują ogień jako proces ciągły (`IsPermanent() == true`). Płomień nie wygasa z upływem czasu, lecz pali się do momentu fizycznego zniszczenia struktury lub ugaszenia cieczą chłodzącą (`Wet`).
2. **Ortogonalna Topologia Rozprzestrzeniania (`SurfaceGridGeometryUtils::FindSpreadCandidates`):**
   - **`Coplanar`:** Ogień i płyny rozchodzą się w płaszczyźnie tej samej ściany lub podłogi (kierunki $\pm U, \pm V$). Jeśli ściany stoją w szeregu, płomień płynnie przechodzi między nimi.
   - **`Corner 90°` (Wielowariantowe dopasowanie krawędzi):** Na styku dwóch prostopadłych płaszczyzn metoda `GetCornerCandidateCoords` zwraca 4 legalne warianty wokseli brzegowych: $(X, Y, Z)$, $(X \pm 1, Y, Z)$, $(X, Y, Z \pm 1)$, $(X \pm 1, Y, Z \pm 1)$.
     - **Priorytet 1 (ActiveCells):** Przeszukanie 4 wariantów w `ActiveCells`. Jeśli na suficie, posadzce lub ścianie istnieje już aktywna komórka (np. rozlany olej na suficie w $Z=18$), zostaje natychmiast wybrana i zapalona bez potrzeby rzucania promieni.
     - **Priorytet 2 (Sondowanie świata):** W przypadku surowego podłoża (np. drewniany strop) promienie `QuerySurface` sondują architekturę, a funkcja `FromWorldLocation(Hit.ImpactPoint)` precyzyjnie kanonizuje pozycję do właściwego woksela.
     - **Filtrowanie ścian płaskich (`bIsWallToWall`):** Zapobieganie fałszywemu skręcaniu prostej ściany pod kątem 90° w puste powietrze działa tylko dla pionowych szwów ściennych (ściana $\leftrightarrow$ ściana), nie blokując naturalnych narożników ze stropem (`Down`) czy posadzką (`Up`).
   - **Zasada Separacji Struktur:** `CornerActor != SourceActor` — obiekt architektury nie podpala bocznych szczelin ani spodu samego siebie (chyba że w tym miejscu gracz celowo nałożył już ciecz).
   - **Brak Sztucznych Blokad na Wodę:** Komórki `Wet` nie blokują spreadu sztucznym `if`, lecz uczestniczą w reakcji `Steam_Extinguish`, odparowując wodę i generując parę wodną.
3. **Niezależne Poziomy Mocy (Multi-Tier Cell Statuses):**
   Każdy wpis w komórce (`FSurfaceCellStatusEntry`) przechowuje własny `uint8 Tier`. Pozwala to na precyzyjne rozliczanie obrażeń z różnych żywiołów koegzystujących na jednym kafelku:
   $$\text{BaseDPS} = \text{Config.GetDamagePerSecond(StatusEntry.Tier)}$$
4. **Agregacja Obrażeń Strukturalnych (`ProcessSurfaceStructuralDamage`):**
   Obrażenia DoT komórek są agregowane per unikalny aktor architektury lochu oraz typ obrażeń (`EDamageType::Fire`, `EDamageType::Lightning`), z ograniczeniem `MaxStructuralDPS = 25.0f`. Obrażenia trafiają do `UDamageableComponent`, który aplikuje rezystancje materiałowe.
5. **Czyszczenie po Zawaleniu (`ClearCellsInBounds`):**
   Zniszczenie ściany/podłogi natychmiast wywołuje `ClearCellsInBounds`, czyszcząc wszystkie przypięte komórki w czasie $O(1)$.

---

## 6. Wydajność i Skalowalność

Układ zaprojektowano pod kątem stabilnych 60 FPS w sesjach kooperacyjnych (1–6 graczy, dziesiątki wrogów, intensywne reakcje żywiołowe):

1. **Siatka Powierzchniowa (Surface Grid):**
   - Rzadka mapa `TMap` przechowuje wyłącznie aktywne komórki.
   - Pętla serwera `ProcessGridTick` (0.25 s) iteruje tylko po kafelkach posiadających aktywny status.
   - Brak alokacji dodatkowych aktorów przy rozlewaniu cieczy na podłodze i ścianach.

2. **Strefy Wolumetryczne (Volumetric Zones):**
   - Po utworzeniu natychmiast przechodzą w stan uśpienia sieciowego `DORM_DormantAll`.
   - Replikacja oparta o stały czas zakończenia `ServerEndTime` (Zero-Bandwidth).
   - Zone Merging (`MergeWithZone`) zapobiega mnożeniu instancji stref tego samego żywiołu w tym samym miejscu.

3. **Komponent Statusów Aktorów (`UStatusEffectComponent`):**
   - Modułowa architektura oparta na wyodrębnionych funkcjach pomocniczych (`ComputeAdjustedDuration`, `DisplaceOtherLiquids`, `SyncDependentStatusesWithCarrier`, `UpsertStatus`).
   - Linearny przebieg `ApplyStatus` bez zduplikowanych pętli i if-ologii.
   - Zero-Tick Idle: komponent wyłącza swój tick, gdy brak aktywnych statusów.

4. **Bezpieczeństwo Wywołań i Brak Zapętleń:**
   - Flaga `bCheckOverlappingZones = false` w podrzędnych wywołaniach `ApplyStatusToCell` gwarantuje maksymalną głębokość stosu równą 1 przy natychmiastowych reakcjach cieczy ze strefami.
   - W pętli `ProcessGridTick` modyfikowane koordynaty są buforowane w lokalnej tablicy `CellsToAffect` przed aplikacją, co zapobiega modyfikacji kontenera w trakcie iteracji.

5. **Optymalizacja Ciągłego Odświeżania i BFS Przewodzenia:**
   - Zastosowanie `MinRemainingToSkip = 1.0f` eliminuje zbędną pracę przy kafelkach posiadających już aktywny status ze znacznym zapasem czasu trwania.
   - Algorytm BFS `PropagateConductionIfApplicable` jest odpalany ściśle warunkowo (`NewlyAddedCount > 0`), co odciąża Game Thread podczas stabilnego stanu strefy.
   - Niezmiennik `Dynamic Conduction Invariant` zapobiega wyciekowi i duplikacji komórek dynamicznych poza obrys siatki mesha.

---

## 7. Roadmapa Rozwoju

| Zadanie | Status | Opis i Cel |
| :--- | :---: | :--- |
| **Surface Grid Subsystem (`UDungeonSurfaceSubsystem`)** | **[ZREALIZOWANE]** | Rzadka siatka komórek 3D na posadzkach i ścianach z obsługą 6 kierunków ścian. |
| **Dekonstrukcja Subsystemu na Zarządcy (`StaticSurfaceGridManager`, `DynamicSurfaceGridManager`)** | **[ZREALIZOWANE]** | Pełna enkapsulacja siatki statycznej i dynamicznej, wyodrębnienie `FSurfaceGridPropagationUtils` i `FSurfaceGridDamageUtils`. |
| **Globalna Propagacja Narożna 90° (`GetCornerCandidateCoords`)** | **[ZREALIZOWANE]** | Rozwiązanie problemu asymetrii sufitu/podłogi dzięki 4-wariantowemu dopasowaniu wokseli brzegowych i kanonizacji pozycji `FromWorldLocation`. |
| **Single Source of Truth (`CalculateElementalTransition`)** | **[ZREALIZOWANE]** | Centralizacja logiki chemicznej, wypierania płynów i czyszczenia statusów w jednej funkcji. |
| **Rozróżnienie Receive vs Sustain (`CanMaterialSustainStatus`)** | **[ZREALIZOWANE]** | Płomień po spaleniu oleju trwa przez pełny czas spalania paliwa na posadzce; prąd zanika bez wody. |
| **Ciągła Integracja Stref z Siatką (`RegisteredStatusZones`)** | **[ZREALIZOWANE]** | Automatyczna rejestracja stref wolumetrycznych w subsystemie; natychmiastowy zapłon plam pod chmurami oraz obsługa ruchomych stref i aur. |
| **Dwukierunkowa Synchronizacja Nośników (`bSyncWithCarrierDuration`)** | **[ZREALIZOWANE]** | Spójna synchronizacja czasów paliwa i nośnika w siatce oraz aktorach (`SyncDependentStatusesWithCarrier`). |
| **Refaktoryzacja i Eliminacja Duplikacji Logiki** | **[ZREALIZOWANE]** | Uproszczenie `ElementalReactionRules.cpp` i `StatusEffectComponent.cpp` do spójnych funkcji pomocniczych (`ComputeAdjustedDuration`, `DisplaceOtherLiquids`, `UpsertStatus`). |
| **Spalanie Paliw Stałych i Niszczenie Architektur (`SolidFuelCombustion`)** | **[ZREALIZOWANE]** | Stałe paliwo drewna (`bSelfSustainingFuel`), ortogonalny spread Coplanar/Corner, odparowywanie wody (`Steam_Extinguish`), niezależne Tiery statusów i agregacja DPS struktur. |
| **Stabilizacja Siatek Dynamicznych i Buforowanie Stref Ciągłych** | **[ZREALIZOWANE]** | Dynamic Conduction Invariant (brak tworzenia komórek poza fizyczną siatką aktora), `MinRemainingToSkip = 1.0f`, selektywny BFS przewodzenia (`NewlyAddedCount > 0`) oraz `Electrified.bCanTransferFromActorToFloor = false`. |
| **Broad-Phase Culling dla Interakcji Aktorów (`ProcessActorInteractions`)** | Planowane | Odrzucanie siatek dynamicznych, których Bounding Box aktora nie przecina testowanego aktora/gracza przed iteracją po lokalnych komórkach. |
| **Component Caching w Obrażeniach Struktur (`ProcessSurfaceStructuralDamage`)** | Planowane | Buforowanie wskaźnika `UDamageableComponent` per zarejestrowany aktor architektury / mechanizmu zamiast każdorazowego przeszukiwania komponentów. |
| **Dynamic Grid Rigid Transform Caching** | Planowane | Pamięć podręczna transformacji sztywnej `GetDynamicRigidTransform()` unieważniana wyłącznie przy rzeczywistej zmianie transformacji komponentu. |
| **Timer Phase Staggering i Sub-Stepping** | Planowane | Mikro-przesunięcie fazy ewaluacji stref wolumetrycznych i komórek w czasie, eliminujące skoki obciążenia w pojedynczych klatkach serwera co 0.25 s. |
| **Wydajnościowy Batching Wizualizacji Debugowej** | Planowane | Zastąpienie periodycznych wywołań `DrawDebugBox` buforowanym rysowaniem w jednym batchu lub dedykowanym komponentem ISM / Niagara przy testach profilowania. |
| **Event-Driven Overlap dla Stref Wolumetrycznych** | Planowane | Zastąpienie periodycznego `GetOverlappingActors` lokalnym zbiorem aktorów w oparciu o delegaty Begin/EndOverlap. |
| **LoS Caching dla Promieni Wybuchu** | Planowane | Pamięć podręczna widoczności celów odświeżana tylko przy przemieszczeniu celu o więcej niż 30 cm. |

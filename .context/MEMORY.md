# Pamięć Projektu i Rejestr Stanu: Dungeon Crawler Co-op

## 1. Metadane Projektu i Kontekst Technologiczny
- **Silnik:** Unreal Engine 5.8 C++ (`MYPROJECT_API`)
- **Gatunek i Skala:** Kooperacyjny dungeon crawler w perspektywie trzecioosobowej / rzut z góry (z płynnym zoomem kamery) dla **1–6 graczy** (Listen Server / P2P).
- **Styl i Architektura:** Enterprise Clean Architecture (standardy Java/Spring Boot):
  - Kompozycja komponentowa (`UActorComponent` jako serwisy domenowe, `ACharacter`/`AActor` jako kontrolery/orkiestratory).
  - Luźne powiązania przez interfejsy (`UInterface` / `IInterface`), całkowity zakaz `Cast<T>` w logice biznesowej.
  - Architektura sterowana zdarzeniami (`DECLARE_DYNAMIC_MULTICAST_DELEGATE`).
  - Standard sieciowy: *Server-Authoritative First*, *Zero-Bandwidth Timers*, deterministyczny *Kinematic Follow with Sweep*.
- **Główny Cel Aktualnego Etapu:** Realizacja poligonu doświadczalnego **Theme Park (Loch v0.1)** zgodnie ze specyfikacją [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md).

---

## 2. Aktualna Struktura Modułów C++ i Klas Projektu

```text
Source/MyProject/
├── Logging/
│   └── DungeonLogCategories.h/.cpp                   [Aktywny] Dedykowane kategorie logowania: LogDungeonInteraction, LogDungeonPhysics, LogDungeonNetwork itp.
│
├── Networking/
│   └── NetworkFunctionLibrary.h/.cpp                 [Aktywny] Makra autorytetu REQUIRE_AUTHORITY, konfiguracja kwantyzacji fizyki, helpery podpinania AttachCarriedProp
│
├── Dungeon/
│   ├── Structure/
│   │   └── DungeonStructureBase.h/.cpp               [Aktywny] Modularne ściany i posadzki, niszczalność, Punch-Through, podatność na żywioły
│   ├── Props/
│   │   ├── InteractivePropBase/                      [Aktywny] Replikowane rekwizyty fizyczne Chaos, transfer pędu, kwantyzacja transformu
│   │   ├── VolatileProp/                             [Aktywny] Niestabilne obiekty alchemiczne, autorytatywne wybuchy, NetMulticast FX
│   │   ├── SwitchPropBase/                           [Aktywny] Baza aktywatorów logicznych, bAllowSwitchBack, TargetMechanisms
│   │   ├── SimpleSwitchProp/                         [Aktywny] Ścienna dźwignia/przełącznik z IInteractableInterface
│   │   └── PressurePlateProp/                        [Aktywny] Podłogowa płyta naciskowa sumująca masę ciał (próg >= 50 kg)
│   └── Mechanisms/
│       ├── MechanismTrapBase/                        [Aktywny] Abstrakcyjna baza pułapek z pętlą czasową bIsContinuousLoop i IMechanismReceiver
│       └── PistonTrap/                               [Aktywny] Kamienny taran ścienny / katapulta / zgniatacz, maszyna EPistonState, Zero-Tick
│
├── Environment/
│   ├── Kinetic/
│   │   ├── Components/KnockbackComponent/            [Aktywny] Replikowany odrzut postaci (LaunchCharacter) i ciał sztywnych (AddImpulse)
│   │   ├── Utilities/KineticForceLibrary             [Aktywny] Radialne eksplozje, wiry kinetyczne, impulsy Chaosu, TryApplyPhysicsPush, SuppressHeavyPhysicsJitter
│   │   └── Enums/KineticEnums.h                      [Aktywny] EKnockbackFalloff
│   ├── Elements/
│   │   ├── Data/StatusEffectDefinitions.h            [Aktywny] Centralny rejestr FStatusEffectRegistry, definicje DoT i reakcji
│   │   ├── Enums/ElementEnums.h                      [Aktywny] EStatusEffectType (Burning, Wet, Electrified, Oiled)
│   │   └── Utilities/
│   │       └── ElementalChemistryLibrary             [Aktywny] Silnik reakcji chemicznych i weryfikacji kompatybilności materiałowej
│   └── Zones/
│       ├── StatusZoneBase.h/.cpp                     [Aktywny] Abstrakcyjna baza cyklu życia strefy, tick serwera 0.25s, DoT, reakcje chemiczne
│       ├── Shapes/
│       │   ├── SurfaceSplashZone.h/.cpp              [Aktywny] Powłoka powierzchniowa (UDecalComponent, 48-ray perimeter, Drop-Off binary search, half-space check)
│       │   └── VolumetricStatusZone.h/.cpp           [Aktywny] Trójwymiarowa sfera statusowa (chmury gazu, kłęby dymu, spowolnienie, brak dekalów)
│       ├── Utilities/
│       │   └── StatusZoneLibrary.h/.cpp              [Aktywny] Zunifikowana biblioteka dostarczania (Point Hit, Surface Splash, Volumetric Zone, Instant Burst)
│       ├── Data/ZoneData.h                           [Aktywny] FZoneEffectConfig (status, instant damage, continuous DoT, movement multiplier)
│       └── Enums/ZoneEnums.h                         [Aktywny] EZoneSpatialShape, EVolatileZoneSpawnMode
│
├── Shared/
│   ├── Components/
│   │   ├── DamageableComponent/                      [Aktywny] Replikowane HP/durability, kinetic impact damage z debouncem (0.25s)
│   │   ├── InteractionComponent/                     [Aktywny] Lekki serwis detekcji (Sphere/Line Trace), Instant & Hold/Channeling (5s) interakcje logiczne
│   │   ├── PhysicsCarryComponent/                    [Aktywny] Dedykowany podsystem niesienia brył Chaos, ECarryState, FCarrySocketConfig, Swing Throw, Forward Throw
│   │   └── StatusEffectComponent/                    [Aktywny] Replikowany rejestr statusów, Zero-Bandwidth Timers, Elemental Priority Pipeline
│   ├── Interfaces/
│   │   ├── CarryAnchorProviderInterface.h            [Aktywny] Kontrakt punktu kotwicy rąk i limitów siły pchania
│   │   ├── IGrabbableInterface.h                     [Aktywny] Kontrakt manipulacji propami fizycznymi
│   │   ├── IInteractableInterface.h                  [Aktywny] Kontrakt interakcji logicznych (dźwignie, przyciski)
│   │   ├── MaterialProviderInterface.h               [Aktywny] Tożsamość materiałowa EPhysicalMaterialType
│   │   ├── MechanismReceiverInterface.h              [Aktywny] Odbiór sygnałów logicznych SetMechanismState
│   │   └── StatProviderInterface.h                   [Aktywny] Zapytania o stan HP/durability
│   └── Enums/
│       └── PhysicalMaterialEnums.h                   [Aktywny] EPhysicalMaterialType (Stone, Wood, Metal, Glass, Flesh)
│
├── Player/
│   ├── Components/PlayerCameraComponent/             [Aktywny] Płynny zoom TPP/Top-Down, On-Demand Tick
│   ├── PlayerCharacter.h/.cpp                        [Aktywny] Kinematyczna postać CMC (ACharacter), Flesh, MoveBlockedBy pchanie fizyczne
│   └── PlayerCharacterController.h/.cpp
│
└── UI/
    ├── PlayerHUD/                                    [Aktywny] Aktor HUD orkiestrujący widgety
    ├── PlayerHUDWidget/                              [Aktywny] Główny widget gracza (pasek HP + kontener statusów)
    ├── StatusIconWidget/                             [Aktywny] Reużywalna kontrolka ikony statusu z radialnym timerem
    └── StatBarWidget.h/.cpp                          [Aktywny] Pasek zdrowia/wytrzymałości
```

---

## 3. Zrealizowane Filary i Ostatnie Zmiany (Stan Faktyczny)

### Filar 1: Postać, Kinetyka i Wzorzec Tarczy (Shield Carry & Physics Decoupling)
- **Kinematyczny Gracz CMC:** Postać dziedziczy z `ACharacter` z wyłączonym `Tick()`. Widoczny cylinder kolizyjny (`SetHiddenInGame(false)`).
- **Fizyczne Pchanie Ciałem (`MoveBlockedBy`):** Gracz ($100\text{ kg}$) przepycha obiekty fizyczne $\le 100\text{ kg}$ z siłą $150\,000\text{ N}$ za pośrednictwem `UKineticForceLibrary::TryApplyPhysicsPush`.
- **Wydzielenie [`UPhysicsCarryComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h):**
  - Całkowite odseparowanie fizyki noszenia brył Chaos od detekcji i klikania przełączników.
  - Posiada strukturę konfiguracyjną `FCarrySocketConfig` (eliminacja Magic Numbers).
  - Trzymany prop nie jest sztywno spinany (`DetachFromActor(KeepWorld)`), lecz podąża kinematycznym sweepem za kotwicą rąk jako **fizyczna tarcza** pochłaniająca ciosy i pociski.
  - **Carry Grip Break:** Automatyczne upuszczenie przedmiotu, gdy ręce oddalą się od zablokowanego propa na $> 70\text{ cm}$.
  - **Dedykowany Rzut na wprost (Klawisz R / LPM):** Autorytatywnie wyliczany przez serwer impuls w kierunku celownika (`ThrowImpulseStrength`).
  - **Rzut Zamachem Myszką & Upuszczenie (Klawisz E):** Upuszczenie pod nogi przy braku ruchu myszą; rzut pędem kątowym przy dynamicznym obrocie kamery.
- **Odchudzony [`UInteractionComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h):**
  - Sphere Trace ($12\text{ cm}$) i Line Trace po dedykowanym kanale `ECC_Interaction` / `ECC_Visibility`.
  - Obsługa przełączników `IInteractableInterface` (natychmiastowych oraz kanałowanych/Hold np. 5s z delegatami postępu dla UI).

### Filar 2: Sieć i Autorytatywność Co-op (Server-Authoritative Co-op)
- **Server-Authoritative First:** Zmiany stanu, zadawanie obrażeń i wyzwalanie reakcji zabezpieczone makrem `REQUIRE_AUTHORITY()`.
- **Dwuwarstwowa Walidacja RPC:** Strukturalna w `_Validate`, domenowa w `_Implementation` (`CanGrabServer`).
- **Anti-Spam Rate Limiting:** Cooldown $0.15\text{ s}$ na żądania interakcji zarówno u klienta, jak i na serwerze.
- **Zero-Bandwidth Timers:** Replikacja wyłącznie `ServerEndTime`. UI lokalnie odlicza upływ czasu, co gwarantuje 0 bajtów/s podczas trwania statusów.
- **Kwantyzacja i Uśpienie Ruchu:** Kwantyzacja `LocationQuantizationLevel = RoundTwoDecimals` oraz `RotationQuantizationLevel = ByteComponents`.

### Filar 3: Reaktywny Silnik Żywiołów i Dystrybucji
- **Elemental Priority Pipeline:**
  - Faza 1: Żywioł vs Powłoka (Ogień trafia w Mokry cel $\rightarrow$ `Steam_Extinguish`, odparowanie i ugaszenie ognia).
  - Faza 2: Żywioł vs Tożsamość Materiałowa (suchy kamień odrzuca ogień, drewno płonie).
- **Silnik Dystrybucji (`UElementalDeliveryLibrary`):** 4 archetypy dostarczania żywiołów (Point Hit, Surface Splash, Radial Burst, Zone).

### Filar 4: Telemetria i Logowanie
- **Dedykowane Kategorie Logowania:** Usunięto użycie `LogTemp`. Cały projekt raportuje do wyodrębnionych kategorii w [`DungeonLogCategories.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Logging/DungeonLogCategories.h) (`LogDungeonInteraction`, `LogDungeonPhysics`, `LogDungeonNetwork`, `LogDungeonMechanisms`, `LogDungeonElements`).

### Filar 5: Modułowy System Stref Gameplayowych (Status Zone System & Multi-Surface Fix)
- **Hierarchia C++ o Pojedynczej Odpowiedzialności (Wariant A):**
  - Rozbicie monolitycznego aktora strefy na czystą hierarchię klas: `AStatusZoneBase` (baza cyklu życia, autorytatywny timer 0.25s, DoT, reakcje chemiczne), `ASurfaceSplashZone` (powłoka powierzchniowa, UDecalComponent, 48-promieniowy obrys z Drop-Off binary edge search, weryfikacja półprzestrzeni), `AVolumetricStatusZone` (lekka sfera 3D, zero dekalów, zero tablic wierzchołków obrysu) oraz bibliotekę fabryczną `UStatusZoneLibrary` z bezstanowym `ApplyInstantBurst` w $t_0$.
- **Rozwiązanie Konfliktu Multi-Surface Splashes (Podłoga vs Ściana):**
  - Wyeliminowano przedwczesne niszczenie świeżo zespawnowanych plam podłogowych przez plamy ścienne wygenerowane w tej samej klatce. Wprowadzono zakaz samoniszczenia dla identycznych żywiołów oraz wymóg ścisłej koplanarności przy wypieraniu cieczy (`NormalDot > 0.85` oraz dystans płaszczyzny $< 30\text{ cm}$).
- **Krytyczny Audyt Skalowalności i Wdrożenie Zone Merging:**
  - Zaimplementowano **Zone Merging & Refresh**: trafienie tym samym żywiołem na tę samą powierzchnię nie tworzy nowego aktora, lecz odświeża czas i powiększa promień istniejącej strefy o 20% z przeliczeniem obrysu 48 promieni (eliminacja GPU Decal Overdraw).
  - Wprowadzono leniwą inwalidację cache obrysu (`IsPerimeterCacheValid`) oraz szybką interpolację kątową promienia ($O(1)$) z limitem 0.0f (brak lewitacji na krawędziach).
  - **Refaktoryzacja Czystej Architektury `ASurfaceSplashZone`:** Rozbito monolityczne metody obliczeniowe na prywatne funkcje pomocnicze (`CheckSurfacePresentAt`, `TraceObstacleDistance`, `FindDropOffEdgeDistance`, `IsWithinNormalBounds`, `IsWithinTangentialPerimeter`), scentralizowano transformację dekalów w `UpdateDecalTransform()`, a weryfikację koplanarności ujednolicono w `IsCoplanarWithPoint` / `IsCoplanarWithZone`.
  - **Usprawnienie 1-Hit Explosion (`ApplyInstantBurst` / `HasExplosionLineOfSight`):**
    - Wprowadzono **5-punktowy próbnik anatomiczny (Multi-Point Probe)** z optymalizacją Fast-Path w `UKineticForceLibrary::HasExplosionLineOfSight` (środek tułowia, głowa/klatka piersiowa $+70\%$, lewe i prawe ramię $\pm 70\%$, stopy $-70\%$). Wyeliminowano problem chowania się postaci za małymi propami/skrzynkami.
    - Zunifikowano `UStatusZoneLibrary::ApplyInstantBurst` w **Single-Pass Query**: jedno zapytanie overlap na kanałach `Pawn`, `PhysicsBody`, `WorldDynamic`, `WorldStatic` zamiast 4 osobnych sweepów; równoczesna aplikacja obrażeń, odrzutu, statusu o konfigurowalnym `Duration` oraz reakcji chemicznych na sąsiednich strefach.
    - Dodano obsługę niszczenia drewnianych struktur i barykad lochu (`ADungeonStructureBase` na profilu `BlockAll` / `ECC_WorldStatic`).
  - Usunięto przestarzałą klasę wrapper `AStatusZone`. Wszystkie call-site'y korzystają bezpośrednio z dedykowanych klas stref.

---

## 4. Historia Przeglądu Architektury (Audit Complete)
Wszystkie 21 punktów technicznych z audytu projektu zostało z sukcesem zrealizowanych i zintegrowanych w kodzie C++:
1. Pełna walidacja rzutu (klient wysyła intencję, serwer wylicza prędkość).
2. Wielopoziomowa walidacja chwytu `CanGrabServer` (dystans, LoS, masa, gotowość).
3. Zero lokalnego desyncu chwytu (stan `RequestingGrab` z potwierdzeniem serwera).
4. Formalna maszyna stanów `ECarryState`.
5. Kinematyczny sweep realizowany autorytatywnie na serwerze i u lokalnego gracza.
6 & 7. Separacja odpowiedzialności: `UPhysicsCarryComponent` (fizyka Chaos) vs `UInteractionComponent` (logika interakcji/Hold) vs `UKineticForceLibrary` (pchanie brył).
8. Optymalizacja operacji `TickComponent` (Event-Driven Overlaps).
9. Wyeliminowanie Magic Numbers do `FCarrySocketConfig`.
10. Konfigurowalny bezpiecznik programowy jitteru `VelocityStopThreshold`.
11 & 12. Dedykowany kanał `ECC_Interaction` oraz ergonomiczny sweep sferyczny `InteractionTraceRadius`.
13. Zabezpieczenie debug draw makrem `#if ENABLE_DRAW_DEBUG`.
14. Własne kategorie logów `LogDungeon*`.
15 & 16. Rate limiting RPC oraz czysta 2-warstwowa walidacja pakietów sieciowych.
17, 18, 19, 20, 21. Czyste decoupled komponenty i spójna dokumentacja.
*(Punkt 22 - automatyczne testy jednostkowe - odłożony na życzenie użytkownika na rzecz testów ręcznych w PIE).*

---

## 5. Kolejne Zadania (Next Steps)
- **Sprint 4: Theme Park Loch v0.1:**
  1. Implementacja pułapki miotającej pociski [`AProjectileLauncherTrap`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Mechanisms/LauncherTrap/) (strzałki/kule ognia z interwałem czasowym).
  2. Implementacja prefabów propów zgodnie ze specyfikacją: `BP_PlankShield_Wood` (lekka drewniana tarcza) oraz `BP_GlassOrb_*` (szklane kule alchemiczne).
  3. Wdrożenie optymalizacji P1 ze specyfikacji stref (Event-Driven Overlaps oraz Zone Merging) przed skalowaniem do masowych fal AI.


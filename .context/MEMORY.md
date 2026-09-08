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
├── Networking/
│   └── NetworkFunctionLibrary.h/.cpp                [Aktywny] Makra autorytetu REQUIRE_AUTHORITY, konfiguracja kwantyzacji fizyki, helpery podpinania
│
├── Dungeon/
│   ├── Structure/
│   │   └── DungeonStructureBase.h/.cpp              [Aktywny] Modularne ściany i posadzki, niszczalność, Punch-Through, podatność na żywioły
│   ├── Props/
│   │   ├── InteractivePropBase/                     [Aktywny] Replikowane rekwizyty fizyczne Chaos, transfer pędu, kwantyzacja transformu
│   │   ├── VolatileProp/                            [Aktywny] Niestabilne obiekty alchemiczne, autorytatywne wybuchy, NetMulticast FX
│   │   ├── SwitchPropBase/                          [Aktywny] Baza aktywatorów logicznych, bAllowSwitchBack, TargetMechanisms
│   │   ├── SimpleSwitchProp/                        [Aktywny] Ścienna dźwignia/przełącznik z IInteractableInterface
│   │   └── PressurePlateProp/                       [Aktywny] Podłogowa płyta naciskowa sumująca masę ciał (próg >= 50 kg)
│   └── Mechanisms/
│       ├── MechanismTrapBase/                       [Aktywny] Abstrakcyjna baza pułapek z pętlą czasową bIsContinuousLoop i IMechanismReceiver
│       └── PistonTrap/                              [Aktywny] Kamienny taran ścienny / katapulta / zgniatacz, maszyna EPistonState, Zero-Tick
│
├── Environment/
│   ├── Kinetic/
│   │   ├── Components/KnockbackComponent/           [Aktywny] Replikowany odrzut postaci (LaunchCharacter) i ciał sztywnych (AddImpulse)
│   │   ├── Utilities/KineticForceLibrary            [Aktywny] Radialne eksplozje, wiry kinetyczne, impulsy Chaosu
│   │   └── Enums/KineticEnums.h                     [Aktywny] EKnockbackFalloff
│   └── Elements/
│       ├── Data/StatusEffectDefinitions.h           [Aktywny] Centralny rejestr FStatusEffectRegistry, definicje DoT i reakcji
│       ├── Enums/ElementEnums.h                     [Aktywny] EStatusEffectType (Burning, Wet, Electrified, Oiled)
│       ├── StatusZone/ElementalStatusZone           [Aktywny] Aktor kałuż/stref pożaru, LoS perimeter trace, progi wysokości, eliminacja konfliktów
│       └── Utilities/
│           ├── ElementalChemistryLibrary            [Aktywny] Silnik reakcji chemicznych i weryfikacji kompatybilności materiałowej
│           └── ElementalDeliveryLibrary             [Aktywny] 4 archetypy dostarczania żywiołów: Point Hit, Surface Splash, Radial Burst, Zone
│
├── Shared/
│   ├── Components/
│   │   ├── DamageableComponent/                     [Aktywny] Replikowane HP/durability, kinetic impact damage z debouncem (0.25s)
│   │   ├── InteractionComponent/                    [Aktywny] Kinematic Sweep Shield, maszyna ECarryState, Anti-Bulldozer, Swing Throw
│   │   └── StatusEffectComponent/                   [Aktywny] Replikowany rejestr statusów, Zero-Bandwidth Timers, Elemental Priority Pipeline
│   ├── Interfaces/
│   │   ├── CarryAnchorProviderInterface.h           [Aktywny] Kontrakt punktu kotwicy rąk i limitów siły pchania
│   │   ├── IGrabbableInterface.h                    [Aktywny] Kontrakt manipulacji propami fizycznymi
│   │   ├── IInteractableInterface.h                 [Aktywny] Kontrakt interakcji klawiszem E
│   │   ├── MaterialProviderInterface.h              [Aktywny] Tożsamość materiałowa EPhysicalMaterialType
│   │   ├── MechanismReceiverInterface.h             [Aktywny] Odbiór sygnałów logicznych SetMechanismState
│   │   └── StatProviderInterface.h                  [Aktywny] Zapytania o stan HP/durability
│   └── Enums/
│       └── PhysicalMaterialEnums.h                  [Aktywny] EPhysicalMaterialType (Stone, Wood, Metal, Glass, Flesh)
│
├── Player/
│   ├── Components/PlayerCameraComponent/            [Aktywny] Płynny zoom TPP/Top-Down, On-Demand Tick
│   ├── PlayerCharacter.h/.cpp                       [Aktywny] Kinematyczna postać CMC (ACharacter), Flesh, MoveBlockedBy pchanie fizyczne
│   └── PlayerCharacterController.h/.cpp
│
└── UI/
    ├── PlayerHUD/                                   [Aktywny] Aktor HUD orkiestrujący widgety
    ├── PlayerHUDWidget/                             [Aktywny] Główny widget gracza (pasek HP + kontener statusów)
    ├── StatusIconWidget/                            [Aktywny] Reużywalna kontrolka ikony statusu z radialnym timerem
    └── StatBarWidget.h/.cpp                         [Aktywny] Pasek zdrowia/wytrzymałości
```

---

## 3. Zrealizowane Filary i Ostatnie Zmiany (Stan Faktyczny)

### Filar 1: Postać, Kinetyka i Wzorzec Tarczy (Shield Carry)
- **Kinematyczny Gracz CMC:** Postać dziedziczy z `ACharacter` z wyłączonym `Tick()`. Wyeliminowano niestabilne ragdolle i wybuchy fizyki Chaosu.
- **Fizyczne Pchanie Ciałem (`MoveBlockedBy`):** Gracz ($100\text{ kg}$) przepycha obiekty fizyczne $\le 100\text{ kg}$ z siłą $150\,000\text{ N}$.
- **Kinematic Sweep Follow (Tarcza):**
  - Trzymany prop nie jest sztywno spinany (`DetachFromActor(KeepWorld)`), lecz podąża kinematycznym sweepem za kotwicą rąk.
  - Działa jako **fizyczna tarcza** pochłaniająca uderzenia i pociski (`ECC_WorldDynamic`).
  - **Decoupling:** [`UInteractionComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h) komunikuje się przez [`ICarryAnchorProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h), co umożliwia trzymanie przedmiotów dowolnemu aktorowi (gracz, humanoid AI).
  - **Anti-Bulldozer Clamp:** Zerowanie prędkości Chaosu przy najechaniu na obiekty $> 100\text{ kg}$.
  - **Carry Grip Break:** Automatyczne upuszczenie przedmiotu, gdy ręce oddalą się od zablokowanego propa na $> 70\text{ cm}$.
  - **Camera Angular Swing Throw & Pure Drop:** W spoczynku upuszczenie pod nogi z zerową prędkością; przy zamachu myszą pęd kątowy nadaje propowi lot po łuku; LPM wykonuje mocny rzut w przód.

### Filar 2: Sieć i Autorytatywność Co-op (Server-Authoritative Co-op)
- **Server-Authoritative First:** Zmiany stanu, zadawanie obrażeń i wyzwalanie reakcji zabezpieczone makrem `REQUIRE_AUTHORITY()`.
- **Server RPCs Interakcji:** `Server_RequestGrab`, `Server_RequestForwardThrow`, `Server_RequestDropOrSwing`, `Server_RequestInteract` z pełną walidacją `_Validate` po stronie serwera.
- **Zero-Bandwidth Timers:** Replikacja wyłącznie `ServerEndTime`. UI lokalnie odlicza upływ czasu, co gwarantuje 0 bajtów/s podczas trwania statusów.
- **Kwantyzacja i Uśpienie Ruchu:** Kwantyzacja `LocationQuantizationLevel = RoundTwoDecimals` oraz `RotationQuantizationLevel = ByteComponents`. Rekwizyty w spoczynku przechodzą w `DORM_DormantAll`.

### Filar 3: Reaktywny Silnik Żywiołów i Dystrybucji
- **Elemental Priority Pipeline:**
  - Faza 1: Żywioł vs Powłoka (Ogień trafia w Mokry cel $\rightarrow$ `Steam_Extinguish`, odparowanie i ugaszenie ognia).
  - Faza 2: Żywioł vs Tożsamość Materiałowa (suchy kamień odrzuca ogień, drewno płonie).
- **Silnik Dystrybucji ([`UElementalDeliveryLibrary`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Elements/Utilities/ElementalDeliveryLibrary.h)):**
  - Point Hit, Surface Splash, Radial Burst (z LoS trace zapobiegającym przenikaniu przez ściany), Status Zone.
- **Strefy Żywiołowe ([`AElementalStatusZone`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Elements/StatusZone/ElementalStatusZone.h)):**
  - Autonomiczny aktor dla rozlewisk wody/oleju i pożarów.
  - Wyliczanie obrysu LoS na ścianach (`RebuildPerimeterPoints`).
  - Rozwiązywanie konfliktów: nowszy płyn nadpisuje stary w obszarze wspólnym.
  - Progi wysokości: ciecz $35\text{ cm}$, ogień $85\text{ cm}$.

### Filar 4: Mechanizmy i Pułapki Lochu (Wdrożenie Theme Park)
- **[`IMechanismReceiverInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/MechanismReceiverInterface.h):** Zunifikowany kontrakt `SetMechanismState` dla odbiorników sygnałów logicznych.
- **[`ASwitchPropBase`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Props/SwitchPropBase/SwitchPropBase.h) & [`ASimpleSwitchProp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Props/SimpleSwitchProp/SimpleSwitchProp.h):** Replikowane przełączniki z powiadamianiem `TargetMechanisms`, obsługą trybu jednostronnego (`bAllowSwitchBack = false`).
- **[`APressurePlateProp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Props/PressurePlateProp/PressurePlateProp.h):** Fizyczna płyta naciskowa autorytatywnie sumująca masę ciał (`RequiredMass = 50 kg`).
- **[`AMechanismTrapBase`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Mechanisms/MechanismTrapBase/MechanismTrapBase.h):** Baza pułapek z pętlą ciągłą (`bIsContinuousLoop`, `LoopInterval`, `InitialDelay`) lub trybem wyzwalanym.
- **[`APistonTrap`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Mechanisms/PistonTrap/PistonTrap.h):** Kamienny taran ścienny / katapulta / zgniatacz. Zero-Tick suw ($1200\text{ cm/s}$ wysunięcie, $150\text{ cm/s}$ powrót), strefa kolizji `DamageBox`, odrzut gracza (`LaunchCharacter`) i impulsy fizyczne (`AddImpulse`).

### Filar 5: Integralność Fizyczna i Niszczalne Struktury
- **Debounce Obrażeń Kinetycznych:** W [`UDamageableComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/DamageableComponent/DamageableComponent.h) wprowadzono `KineticImpactCooldown = 0.25s`, eliminując wielokrotne obrażenia w pojedynczym zderzeniu.
- **Modularne Struktury Lochu ([`ADungeonStructureBase`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Structure/DungeonStructureBase.h)):** Implementacja Świętej Trójcy (`DamageableComponent`, `MaterialType`, podatność na statusy), niszczenie z zachowaniem pędu przebicia (Punch-Through).

---

## 4. Status Realizacji [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md)

| Zadanie / Komponent | Typ | Status | Szczegóły implementacji |
| :--- | :--- | :---: | :--- |
| **`APistonTrap` (Taran/Tłok)** | C++ Class | ✅ Ukończone | Klasa w `Dungeon/Mechanisms/PistonTrap/`, maszyna `EPistonState`, Zero-Tick, Knockback |
| **`APressurePlateProp` (Płyta naciskowa)** | C++ Class | ✅ Ukończone | Klasa w `Dungeon/Props/PressurePlateProp/`, ważenie fizycznej masy >= 50 kg |
| **`ASwitchPropBase` / `ASimpleSwitchProp`** | C++ Class | ✅ Ukończone | Klasy w `Dungeon/Props/`, obsługa `TargetMechanisms`, Co-op replikacja |
| **`AProjectileLauncherTrap` (Miotacz głazów)**| C++ Class | ⏳ Do wdrożenia | Specyfikacja zdefiniowana w THEME_PARK_SPECIFICATION.md §5.2 |
| **`BP_GlassOrb_*` (Magiczne Kule Szkła)** | Blueprint | ⏳ Do wdrożenia | Zastąpienie archaicznych beczek szklanymi bombami alchemicznymi |
| **`BP_PlankShield_Wood` (Prowizoryczna tarcza)**| Blueprint | ⏳ Do wdrożenia | Deska z uchwytem (18-24 kg, Durability=50) pod korytarz miotacza głazów |
| **`BP_Stone_Small/Medium/Heavy`** | Blueprint | ⏳ Do wdrożenia | Taksonomia mas kamieni: 8-15 kg (rzut), 60-85 kg (pchanie), 250-400 kg (niszczyciel) |
| **Złożenie Poziomu Theme Park (4 Komnaty)** | Poziom UE | ⏳ Do wdrożenia | Siatka modularna 400x400x20 cm, Komnaty 1–4, weryfikacja w PIE Listen Server |

---

## 5. Złote Reguły i Konwencje Projektu

1. **Święta Trójca Propów i Struktur:**
   - Każdy obiekt w lochu musi posiadać: `IMaterialProviderInterface`, `UDamageableComponent`, `UStatusEffectComponent`. Zakaz pustych Static Meshów.
2. **Standard Siatki Modularnej:**
   - Podłoga/Sufit: `400 x 400 x 20 cm`
   - Ściana Pełna: `400 x 30 x 350 cm`
   - Wnęka Drzwiowa: `200 x 30 x 280 cm`
   - Kolumna/Narożnik: `40 x 40 x 350 cm`
   - Wszystkie pivoty na krawędzi modułu na poziomie $Z = 0$.
3. **Zero-Bandwidth Mindset:**
   - Czas trwania replikujemy wyłącznie jako `ServerEndTime`. Żadnych floatów malejących w `Tick()`.
4. **Bezpieczeństwo Chaosu (Anti-Bulldozer):**
   - Obiekty trzymane prowadzone kinematycznym sweepem. Przy kontakcie z masą $> 100\text{ kg}$ prędkości fizyki są tłumione, a przy rozciągnięciu $> 70\text{ cm}$ następuje zerwanie chwytu.

---

## 6. Najbliższe Kroki (Roadmap)

1. **Implementacja C++: `AProjectileLauncherTrap`**
   - Dziedziczenie z `AMechanismTrapBase`.
   - Parametry: `ProjectileClass`, `LaunchSpeed`, `FireRate`, `bTriggeredBySwitch`.
   - Spawnowanie i wyrzut pocisków/kul w stronę korytarza testowego.
2. **Konfiguracja Blueprintów Produkcyjnych:**
   - Utworzenie `BP_PlankShield_Wood` z właściwą tożsamością materiałową `Wood` i parametrami absorpcji.
   - Utworzenie Magicznych Kul Szklanych `BP_GlassOrb_Fire`, `BP_GlassOrb_Water`, `BP_GlassOrb_Oil`, `BP_GlassOrb_Lightning` wykorzystujących `UElementalDeliveryLibrary`.
   - Przygotowanie prefabów modularnych ścian i posadzek `BP_Wall_Solid_Stone`, `BP_Wall_Breachable_Stone`, `BP_Barricade_Wood`, `BP_Floor_Dungeon_Stone`, `BP_Floor_Cracked_Fallthrough`.
3. **Konstrukcja Poziomu Theme Park (Loch v0.1):**
   - Montaż Komnaty 1 (Kinetyka i Tarcza), Komnaty 2 (Alchemia i Rozlewiska), Komnaty 3 (Niszczenie i Skarpa), Komnaty 4 (Świątynia, Płyta 50kg, Krata).
   - Testy rozgrywki w PIE (Listen Server + 2 graczy).

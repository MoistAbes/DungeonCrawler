# Podsumowanie Stanu Projektu: System Kinetyki, Struktury Lochu i Interakcje (Dungeon Crawler)

## 1. Kontekst Projektu
- **Silnik:** Unreal Engine 5.8, C++ (`MYPROJECT_API`)
- **Gatunek:** Kooperacyjny dungeon crawler w widoku z góry / TPP (z płynnym zoomem kamery)
- **Filozofia Fizyki i Świata:** 
  - Architektura oparta na kompozycji (`ActorComponents` / `@Service`-style), determinizmie oraz interfejsach biznesowych.
  - Wyraźny podział na **statyczną geometrię i propy lochu (`Dungeon/`)**, **prawa fizyki i żywiołów (`Environment/`)** oraz **uniwersalne mechaniki współdzielone (`Shared/`)**.

---

## 2. Architektura Folderów i Modułów (Aktualna)

```text
Source/MyProject/
├── Dungeon/                                      <-- Zawartość i architektura lochu
│   ├── Structure/
│   │   └── DungeonStructureBase.h/.cpp           (Modularne ściany, podłogi, niszczalne moduły)
│   └── Props/
│       ├── InteractivePropBase/                  (Fizyczne rekwizyty lochu, wazy, skrzynki)
│       ├── ExplosiveBarrelProp/                  (Wybuchające beczki detonujące radialnie)
│       └── SimpleSwitchProp/                     (Dźwignie, przełączniki, mechanizmy)
│
├── Shared/                                       <-- Uniwersalne mechaniki, komponenty i kontrakty
│   ├── Components/
│   │   ├── DamageableComponent/                  (Uniwersalny komponent zdrowia/durability)
│   │   └── InteractionComponent/                 (Wykrywanie raycastem, chwytanie fizyczne i interakcja logiczna dla gracza i AI)
│   ├── Interfaces/
│   │   ├── IInteractableInterface.h              (Kontrakt na aktywację klawiszem/AI)
│   │   ├── IGrabbableInterface.h                 (Kontrakt na chwytanie PhysicsHandle)
│   │   ├── MaterialProviderInterface.h           (Zapytanie o tożsamość materiałową)
│   │   └── StatProviderInterface.h               (Zapytanie o stan HP/durability)
│   └── Enums/
│       └── PhysicalMaterialEnums.h               (EPhysicalMaterialType: Stone, Wood, Metal, Glass, Flesh)
│
├── Environment/                                  <-- Prawa świata, kinetyka i żywioły
│   ├── Kinetic/
│   │   ├── Components/
│   │   │   └── KnockbackComponent/               (Aplikowanie sił odrzutu i impulsów)
│   │   ├── Utilities/
│   │   │   └── KineticForceLibrary               (Eksplozje radialne, odrzuty, wiry)
│   │   └── Enums/
│   │       └── KineticEnums.h                    (EKnockbackFalloff)
│   └── Elements/
│       └── Enums/
│           └── ElementEnums.h                    (EStatusEffectType: Burning, Wet, Electrified, Oiled)
│
├── Player/                                       <-- Kontrola, zmysły i ruch gracza
│   ├── Components/
│   │   └── PlayerCameraComponent/                (Płynny zoom kamery, tick on-demand)
│   ├── PlayerCharacter.h/.cpp                    (Kinematyczny model gracza oparty na CMC)
│   └── PlayerCharacterController
│
└── UI/                                           <-- Prezentacja stanu gry
    ├── PlayerHUD/                                (Aktor HUD reagujący na eventy)
    ├── PlayerHUDWidget/                          (Kontroler widoku łączący pasek zdrowia)
    └── StatBarWidget.h/.cpp
```

---

## 3. Zrealizowane Filary Systemowe

### Filar 1: Architektura Postaci i Kamery
- **`APlayerCharacter`:** Kinematyczny model oparty o standardowy, stabilny `CharacterMovementComponent` (CMC).
  - Całkowite wyłączenie niepotrzebnego `Tick()` na postaci (`PrimaryActorTick.bCanEverTick = false`).
  - Podgląd kolizji kapsuły (`SetHiddenInGame(false)`).
  - Korzysta z `UInteractionComponent`, `UDamageableComponent`, `UKnockbackComponent`.
- **`UPlayerCameraComponent`:** Dedykowany komponent kamery obsługujący płynny zoom w oparciu o `Tick on-demand`.
- **`APlayerHUD`:** Odizolowany aktor HUD zarządzający widgetem `WBP_PlayerHUD`. Reaguje na eventy zmiany postaci (`OnPossessedPawnChanged`) i automatycznie subskrybuje pasek życia.

### Filar 2: System Obrażeń, Odrzutów i Kinetyki
- **`UDamageableComponent`:** 
  - Uniwersalny komponent zdrowia i wytrzymałości (`MaxDurability`, `InitialDurability`, `CurrentDurability`).
  - Automatyczne wiązanie zdarzeń upadku (`LandedDelegate` -> Fall Damage) oraz zderzeń ze ścianami w locie i przy ślizgu po ziemi (`OnComponentHit`).
  - Wykrywanie energii zderzenia na podstawie prędkości prostopadłej (`DotProduct` z normalną ściany).
- **`UKnockbackComponent`:**
  - Obsługa odrzutów dla postaci (przez `LaunchCharacter` z mnożnikiem `AirborneMultiplier`) oraz obiektów fizycznych (`AddImpulse`).
  - Wsparcie dla odporności na odrzut (`KnockbackResistance`) i niewrażliwości (`bIsImmune`).
  - Własny enum `EKnockbackFalloff` (`Linear`, `Constant`).
- **`UKineticForceLibrary`:**
  - `ApplyExplosion`: Obrażenia i radialny odrzut z tłumieniem liniowym.
  - `ApplyDirectionalKnockback`: Kierunkowy odrzut z regulowanym podbiciem w górę (`VerticalLiftRatio`).
  - `ApplyVortexPull`: Wir przyciągający jednostki i obiekty do centrum.

### Filar 3: Struktury Lochu, Tożsamość Materiałowa i Interakcje
- **`EPhysicalMaterialType` w `Shared/Enums`:**
  - Współdzielone przez cały świat: ściany, wazy, graczy i potwory (`Flesh`, `Stone`, `Wood`, `Metal`, `Glass`).
  - Podstawa pod przyszłe reakcje żywiołowe (`Elements`).
- **`UInteractionComponent` w `Shared/Components`:**
  - Działa zarówno dla Gracza, jak i dla AI (używa `GetActorEyesViewPoint`).
  - Obsługuje chwytanie/rzucanie obiektów (`IGrabbableInterface`) oraz aktywację logiczną (`IInteractableInterface`).
- **`ADungeonStructureBase`:** 
  - Klasa bazowa dla podłóg, ścian, sufitów i filarów (`BlockAll`, brak narzutu fizyki Chaos).
  - Konfigurowalna niszczalność (`bIsDestructible = true/false`).
  - **Punch-Through (Przebijanie barykad):** Gdy uderzenie o ścianę przekracza jej HP i ją niszczy, wyłączana jest natychmiast kolizja, a pęd uderzającego obiektu/gracza jest przekazywany dalej za ścianę pomniejszony o opór (`PunchThroughVelocityRetention = 0.6f`).
- **`AInteractivePropBase` & `AExplosiveBarrelProp`:**
  - Fizyczne rekwizyty (`SimulatePhysics = true`), podnoszenie i rzucanie bezwładnościowe (`IGrabbableInterface`).
  - Blokada zadawania obrażeń otoczeniu w trakcie trzymania w rękach (`IsGrabbed() == true`) – eliminacja glitchy przy ocieraniu o ściany.
  - Detonacja wybuchającej beczki po osiągnięciu 0 HP.

---

## 4. Kolejne Kroki i Priorytety

1. **System Reakcji Żywiołowych (Elements / Status Effects):**
   - Komponent reakcji (np. `UStatusEffectComponent`): stany `Burning`, `Wet`, `Electrified`, `Oiled`.
   - Podpalanie drewnianych ścian i wybuchających beczek, reakcje Flesh vs Stone.
2. **Mechanika Walki i Broni Gracza:**
   - Ataki bronią białą (ciosy mieczem/młotem rozbijające słabe punkty otoczenia i odrzucające wrogów).
3. **Logiczne Mechanizmy Lochu:**
   - Dźwignie (`ASimpleSwitchProp`), płyty naciskowe i kraty/drzwi otwierane sygnałem logicznym.

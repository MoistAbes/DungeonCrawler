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
│       ├── VolatileProp/                         (Uniwersalne niestabilne obiekty: miny, bomby, żywiołowe beczki)
│       ├── ExplosiveBarrelProp/                  (Wybuchające beczki - specjalizacja AVolatileProp)
│       └── SimpleSwitchProp/                     (Dźwignie, przełączniki, mechanizmy)
│
├── Shared/                                       <-- Uniwersalne mechaniki, komponenty i kontrakty
│   ├── Components/
│   │   ├── DamageableComponent/                  (Uniwersalny komponent zdrowia/durability)
│   │   ├── InteractionComponent/                 (Wykrywanie raycastem, chwytanie fizyczne i interakcja logiczna dla gracza i AI)
│   │   └── StatusEffectComponent/                (Zarządzanie statusami: Burning, Wet, Electrified, Oiled)
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
│       ├── Utilities/
│       │   └── ElementalChemistryLibrary         (Silnik reakcji chemicznych i kompatybilności materiałowej)
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
    ├── PlayerHUDWidget/                          (Kontroler widoku łączący pasek zdrowia i kontener ikon statusów)
    ├── StatusIconWidget/                         (Reużywalna kontrolka pojedynczej ikony statusu)
    └── StatBarWidget.h/.cpp
```

---

## 3. Zrealizowane Filary Systemowe

### Filar 1: Architektura Postaci i Kamery
- **`APlayerCharacter`:** Kinematyczny model oparty o standardowy, stabilny `CharacterMovementComponent` (CMC).
  - Całkowite wyłączenie niepotrzebnego `Tick()` na postaci (`PrimaryActorTick.bCanEverTick = false`).
  - Podgląd kolizji kapsuły (`SetHiddenInGame(false)`).
  - Posiada tożsamość materiałową `IMaterialProviderInterface` (`EPhysicalMaterialType::Flesh`).
  - Korzysta z `UInteractionComponent`, `UDamageableComponent`, `UKnockbackComponent`, `UStatusEffectComponent`.
- **`UPlayerCameraComponent`:** Dedykowany komponent kamery obsługujący płynny zoom w oparciu o `Tick on-demand`.
- **`APlayerHUD`:** Odizolowany aktor HUD zarządzający widgetem `WBP_PlayerHUD`. Reaguje na eventy zmiany postaci (`OnPossessedPawnChanged`) i automatycznie subskrybuje pasek życia oraz statusy.

### Filar 2: System Obrażeń, Odrzutów i Kinetyki
- **`UDamageableComponent`:** 
  - Uniwersalny komponent zdrowia i wytrzymałości (`MaxDurability`, `InitialDurability`, `CurrentDurability`).
  - Automatyczne wiązanie zdarzeń upadku (`LandedDelegate` -> Fall Damage) oraz zderzeń ze ścianami w locie i przy ślizgu po ziemi oraz uderzeń w sufit (`OnComponentHit`).
  - Wykrywanie energii zderzenia na podstawie prędkości prostopadłej (`DotProduct` z normalną ściany).
- **`UKnockbackComponent`:**
  - Obsługa odrzutów dla postaci (przez `LaunchCharacter` z mnożnikiem `AirborneMultiplier`) oraz obiektów fizycznych (`AddImpulse`).
  - Wsparcie dla odporności na odrzut (`KnockbackResistance`) i niewrażliwości (`bIsImmune`).
  - Własny enum `EKnockbackFalloff` (`Linear`, `Constant`).
- **`UKineticForceLibrary`:**
  - `ApplyExplosion`: Obrażenia i radialny odrzut z tłumieniem liniowym.
  - `ApplyDirectionalKnockback`: Kierunkowy odrzut z regulowanym podbiciem w górę (`VerticalLiftRatio`).
  - `ApplyVortexPull`: Wir przyciągający jednostki i obiekty do centrum.

### Filar 3: Reaktywny System Statusów Żywiołowych (Elements & Status Effects)
- **`UStatusEffectComponent` w `Shared/Components`:**
  - Odpowiada wyłącznie za stan podmiotu: przechowywanie statusów w `TMap<EStatusEffectType, FActiveStatusEffectInstance>`, odliczanie czasu trwania, brak duplikatów i cykl życia.
  - Obrażenia okresowe DoT (`Burning`) aplikowane przez `UDamageableComponent`.
  - Architektura oparta na zdarzeniach (`OnStatusEffectApplied`, `OnStatusEffectRemoved`, `OnElementalReactionTriggered`).
  - Optymalizacja `Tick on-demand`: komponent tickuje tylko wtedy, gdy na celu znajduje się przynajmniej jeden aktywny status.
- **`UElementalChemistryLibrary` w `Environment/Elements/Utilities`:**
  - Dedykowany silnik chemii regułowej (Chemistry Engine).
  - Klasyfikacja i taksonomia powłok: `IsLiquidStatus` (`Wet`, `Oiled`).
  - Weryfikacja kompatybilności materiałowej (`CanMaterialReceiveStatus`).
  - Dwuetapowa ewaluacja reakcji żywiołowych zwracająca DTO `FElementalReactionResult` (`EvaluateReaction`):
    - **Faza 1 (Gwałtowne reakcje o wysokim priorytecie):**
      - `Wet` + `Burning` $\rightarrow$ zgaszenie ognia, odparowanie wody (`Steam_Extinguish` / `Fire_Extinguished`).
      - `Oiled` + `Burning` $\rightarrow$ natychmiastowy zapłon oleju, obrażenia bonusowe (`Oil_Ignition`).
      - `Wet` + `Electrified` $\rightarrow$ szok przewodzący prąd (`Conductive_Shock`).
    - **Faza 2 (Reguła Powłok Płynnych - Liquid Displacement):**
      - Każdy przychodzący płyn (`IsLiquidStatus`) wypiera poprzednio nałożony płyn (`ExistingStatusToRemove = PoprzedniPłyn`, `ReactionTag = Liquid_Displaced`), wykluczając jednoczesne posiadanie np. `Wet` i `Oiled`.

### Filar 4: Struktury Lochu, Tożsamość Materiałowa i Interakcje
- **`EPhysicalMaterialType` w `Shared/Enums`:**
  - Współdzielone przez cały świat: ściany, wazy, graczy i potwory (`Flesh`, `Stone`, `Wood`, `Metal`, `Glass`).
- **`UInteractionComponent` w `Shared/Components`:**
  - Działa zarówno dla Gracza, jak i dla AI (używa `GetActorEyesViewPoint`).
  - Obsługuje chwytanie/rzucanie obiektów (`IGrabbableInterface`) oraz aktywację logiczną (`IInteractableInterface`).
- **`ADungeonStructureBase`:** 
  - Klasa bazowa dla podłóg, ścian, sufitów i filarów (`BlockAll`, brak narzutu fizyki Chaos).
  - Konfigurowalna niszczalność (`bIsDestructible = true/false`).
  - **Punch-Through (Przebijanie barykad):** Gdy uderzenie o ścianę przekracza jej HP i ją niszczy, wyłączana jest natychmiast kolizja, a pęd uderzającego obiektu/gracza jest przekazywany dalej za ścianę pomniejszony o opór (`PunchThroughVelocityRetention = 0.6f`).
- **`AVolatileProp` & `AExplosiveBarrelProp`:**
  - `AVolatileProp`: Niestabilne obiekty lochu (bomby, miny, bańki żywiołów, beczki). Konfigurowalny promień, obrażenia, odrzut, żywioł i czas trwania statusu.
  - `AExplosiveBarrelProp`: Dedykowana specjalizacja `AVolatileProp` gwarantująca pełną kompatybilność z blueprintem `BP_ExplosiveBarrel`.
  - Blokada zadawania obrażeń otoczeniu w trakcie trzymania w rękach (`IsGrabbed() == true`).

### Filar 5: Warstwa Prezentacji i UI (HUD & Status Bar)
- **`UStatusEffectIconWidget`:** Klasa bazowa dla ikony aktywnego statusu (`WBP_StatusIcon`).
  - Posiada opcjonalne bindingi na `IconImage`, `DurationText` i `ProgressBar` roll.
  - Posiada mapę tekstur `StatusTextures` (`TMap<EStatusEffectType, TObjectPtr<UTexture2D>>`) – gdy ikona ma dedykowaną teksturę graficzną, zachowuje oryginalne kolory (`FLinearColor::White`), a w przypadku braku tekstury barwi się domyślnym kolorem żywiołu (`GetDefaultStatusColor`).
- **`UPlayerHUDWidget`:**
  - Dynamicznie spawnuje ikonki statusów w kontenerze `StatusEffectsContainer` (`HorizontalBox`).
  - Kontener ma ustawione `Auto Size = True`, dzięki czemu automatycznie dopasowuje szerokość i wysokość do dowolnej liczby ikonek o dowolnym rozmiarze bez ich deformowania (brak spłaszczania elipsy).
  - Automatycznie aktualizuje czas trwania i usuwa ikonki po wygaszeniu statusu.
  - Odpala eventy `OnStatusEffectAdded` i `OnStatusEffectRemoved` dla logiki Blueprint.

---

## 6. Powiązane Procedury i Dokumentacje (SOP)
- **Pipeline Graficzny i Konfiguracja Ikon Statusu:** Pełna procedura przygotowania grafik PNG 512x512, skryptu usuwania białej poświaty (Alpha Bleed / Fringing), parametrów kompresji w UE i rejestracji w UI znajduje się w dedykowanym dokumencie:  
  👉 [`.context/UI_STATUS_ICONS_PIPELINE.md`](file:///E:/UE_PROJECTS/MyProject/.context/UI_STATUS_ICONS_PIPELINE.md)

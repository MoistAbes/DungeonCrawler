# Podsumowanie Stanu Projektu: System Kinetyki, Struktury Lochu i Interakcje (Dungeon Crawler)

## 1. Kontekst Projektu
- **Silnik:** Unreal Engine 5.8, C++ (`MYPROJECT_API`)
- **Gatunek:** Kooperacyjny dungeon crawler w widoku z góry / TPP (z płynnym zoomem kamery)
- **Filozofia Fizyki i Świata:** 
  - Architektura oparta na kompozycji (`ActorComponents` / `@Service`-style), determinizmie oraz interfejsach biznesowych.
  - Wyraźny podział na **statyczną geometrię lochu**, **niszczalne moduły/struktury** oraz **swobodne propy fizyczne (Chaos)**.

---

## 2. Zrealizowane Moduły i Filary (Stan Obecny)

### Filar 1: Architektura Postaci i Kamery
- **`APlayerCharacter`:** Kinematyczny model oparty o standardowy, stabilny `CharacterMovementComponent` (CMC).
  - Całkowite wyłączenie niepotrzebnego `Tick()` na postaci (`PrimaryActorTick.bCanEverTick = false`).
  - Podgląd kolizji kapsuły (`SetHiddenInGame(false)`).
- **`UPlayerCameraComponent`:** Dedykowany komponent kamery obsługujący płynny zoom w oparciu o `Tick on-demand` (wyłącza swój tick, gdy kamera osiągnie cel).
- **`APlayerHUD`:** Odizolowany, dedykowany aktor HUD zarządzający widgetem `WBP_PlayerHUD`. Reaguje na eventy zmiany postaci (`OnPossessedPawnChanged`) i automatycznie subskrybuje pasek życia.

### Filar 2: System Obrażeń, Odrzutów i Kinetyki
- **`UDamageableComponent`:** 
  - Uniwersalny komponent zdrowia i wytrzymałości (`MaxDurability`, `InitialDurability`, `CurrentDurability`).
  - Automatyczne wiązanie zdarzeń upadku (`LandedDelegate` -> Fall Damage) oraz zderzeń ze ścianami w locie (`OnComponentHit` przy `IsFalling()`).
  - Wykrywanie energii zderzenia na podstawie prędkości prostopadłej (`DotProduct` z normalną ściany).
- **`UKnockbackComponent`:**
  - Obsługa odrzutów dla postaci (przez `LaunchCharacter` z mnożnikiem `AirborneMultiplier`) oraz obiektów fizycznych (`AddImpulse`).
  - Wsparcie dla odporności na odrzut (`KnockbackResistance`) i niewrażliwości (`bIsImmune`).
  - Własny enum `EKnockbackFalloff` (`Linear`, `Constant`).
- **`UCombatForceLibrary`:**
  - `ApplyExplosion`: Obrażenia i radialny odrzut z tłumieniem liniowym.
  - `ApplyDirectionalKnockback`: Kierunkowy odrzut z regulowanym podbiciem w górę (`VerticalLiftRatio`).
  - `ApplyVortexPull`: Wir przyciągający jednostki i obiekty do centrum.

### Filar 3: Struktury Lochu i Tożsamość Materiałowa
- **`IMaterialProviderInterface`:** Interfejs `GetMaterialType()` zwracający `EPhysicalMaterialType` (`Stone`, `Wood`, `Metal`, `Glass`, `Flesh`).
- **`ADungeonStructureBase`:** 
  - Klasa bazowa dla podłóg, ścian, sufitów i filarów.
  - Domyślnie statyczna, stabilna kolizja `BlockAll` (brak narzutu fizyki Chaos).
  - Konfigurowalna niszczalność (`bIsDestructible = true/false`).
  - **Punch-Through (Przebijanie barykad):** Gdy uderzenie o ścianę przekracza jej HP i ją niszczy, wyłączana jest natychmiast kolizja, a pęd uderzającego obiektu/gracza jest przekazywany dalej za ścianę pomniejszony o opór (`PunchThroughVelocityRetention = 0.6f`).
- **`AInteractivePropBase` & `AExplosiveBarrelProp`:**
  - Fizyczne rekwizyty (`SimulatePhysics = true`), podnoszenie i rzucanie bezwładnościowe (`IGrabbableInterface`).
  - Blokada zadawania obrażeń otoczeniu w trakcie trzymania w rękach (`IsGrabbed() == true`) – eliminacja glitchy przy ocieraniu o ściany.
  - Detonacja wybuchającej beczki po osiągnięciu 0 HP.

---

## 3. Kolejne Kroki i Priorytety

1. **System Reakcji Żywiołowych (Elements / Status Effects):**
   - Komponent reakcji (np. `UStatusEffectComponent`): stany `Burning`, `Wet`, `Electrified`, `Oiled`.
   - Podpalanie drewnianych ścian i wybuchających beczek, rozprzestrzenianie ognia.
2. **Mechanika Walki i Broni Gracza:**
   - Ataki bronią białą (ciosy mieczem/młotem rozbijające słabe punkty otoczenia i odrzucające wrogów).
3. **Logiczne Mechanizmy Lochu:**
   - Dźwignie (`ASimpleSwitchProp`), płyty naciskowe i kraty/drzwi otwierane sygnałem logicznym.

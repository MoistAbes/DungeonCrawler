# Podsumowanie Stanu Projektu: System Kinetyki, Struktury Lochu i Interakcje (Dungeon Crawler)

## 1. Kontekst Projektu
- **Silnik:** Unreal Engine 5.8, C++ (`MYPROJECT_API`)
- **Gatunek i Skala:** Kooperacyjny dungeon crawler w widoku z góry / TPP (z płynnym zoomem kamery) dla **1–6 graczy** (Listen Server / P2P).
- **Filozofia Fizyki i Świata:** 
  - Architektura oparta na kompozycji (`ActorComponents` / `@Service`-style), determinizmie oraz interfejsach biznesowych.
  - Wyraźny podział na **statyczną geometrię i propy lochu (`Dungeon/`)**, **prawa fizyki i żywiołów (`Environment/`)**, **uniwersalne mechaniki współdzielone (`Shared/`)** oraz **odizolowaną warstwę sieciową (`Networking/`)**.
  - Standard Developer Experience: Ujednolicone kategorie Details (`Custom|...`) oraz wyczerpujące opisy tooltips w Blueprintach.
  - **Standard Sieciowy:** *Server-Authoritative First*, *Zero-Bandwidth Timers*, deterministyczna replikacja propów bez jittera fizyki.

---

## 2. Architektura Folderów i Modułów (Aktualna)

```text
Source/MyProject/
├── Networking/                                    <-- Odizolowana domena sieciowa (Co-op 1–6 graczy)
│   ├── NetworkFunctionLibrary.h/.cpp              (Biblioteka pomocnicza: NetUtils::HasAuthority, NetUtils::GetNetRolePrefix, IsLocallyControlled)
│
├── Dungeon/                                       <-- Zawartość i architektura lochu
│   ├── Structure/
│   │   └── DungeonStructureBase.h/.cpp            (Modularne ściany, podłogi, niszczalne moduły, Punch-Through)
│   └── Props/
│       ├── InteractivePropBase/                   (Fizyczne rekwizyty lochu, wazy, skrzynki, Kinetic Impact Transfer)
│       ├── VolatileProp/                          (Uniwersalne niestabilne obiekty: miny, bomby, żywiołowe beczki)
│       ├── ExplosiveBarrelProp/                   (Wybuchające beczki - specjalizacja AVolatileProp)
│       └── SimpleSwitchProp/                      (Dźwignie, przełączniki, mechanizmy)
│
├── Shared/                                        <-- Uniwersalne mechaniki, komponenty i kontrakty
│   ├── Components/
│   │   ├── DamageableComponent/                   (Replikowany komponent zdrowia/durability, autorytet serwera)
│   │   ├── InteractionComponent/                  (Wykrywanie raycastem, chwytanie fizyczne i interakcja logiczna)
│   │   └── StatusEffectComponent/                 (Zarządzanie czasem trwania i cyklem życia instancji statusów)
│   ├── Interfaces/
│   │   ├── IInteractableInterface.h               (Kontrakt na aktywację klawiszem/AI)
│   │   ├── IGrabbableInterface.h                  (Kontrakt na chwytanie PhysicsHandle)
│   │   ├── MaterialProviderInterface.h            (Zapytanie o tożsamość materiałową)
│   │   └── StatProviderInterface.h                (Zapytanie o stan HP/durability)
│   └── Enums/
│       └── PhysicalMaterialEnums.h                (EPhysicalMaterialType: Stone, Wood, Metal, Glass, Flesh)
│
├── Environment/                                   <-- Prawa świata, kinetyka i żywioły
│   ├── Kinetic/
│   │   ├── Components/
│   │   │   └── KnockbackComponent/                (Aplikowanie sił odrzutu i impulsów dla postaci i fizyki)
│   │   ├── Utilities/
│   │   │   └── KineticForceLibrary                (Eksplozje radialne, odrzuty, wiry, pęd fizyczny Chaos)
│   │   └── Enums/
│   │       └── KineticEnums.h                     (EKnockbackFalloff)
│   └── Elements/
│       ├── Data/
│       │   └── StatusEffectDefinitions.h          (Centralny rejestr FStatusEffectRegistry, reguły reakcji i DoT)
│       ├── Utilities/
│       │   └── ElementalChemistryLibrary          (Silnik reakcji chemicznych i kompatybilności materiałowej)
│       └── Enums/
│           └── ElementEnums.h                     (EStatusEffectType: Burning, Wet, Electrified, Oiled)
│
├── Player/                                        <-- Kontrola, zmysły i ruch gracza
│   ├── Components/
│   │   └── PlayerCameraComponent/                 (Płynny zoom kamery, tick on-demand)
│   ├── PlayerCharacter.h/.cpp                     (Kinematyczny model gracza oparty na CMC, Enhanced Input)
│   └── PlayerCharacterController
│
└── UI/                                            <-- Prezentacja stanu gry
    ├── PlayerHUD/                                 (Aktor HUD reagujący na eventy)
    ├── PlayerHUDWidget/                           (Kontroler widoku łączący pasek zdrowia i kontener ikon statusów)
    ├── StatusIconWidget/                          (Reużywalna kontrolka pojedynczej ikony statusu)
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
  - W pełni skonfigurowany pod `Enhanced Input` (akcje `MoveAction`, `LookAction`, `ZoomAction`, `InteractAction`, `ThrowAction`, `JumpAction`).
- **`UPlayerCameraComponent`:** Dedykowany komponent kamery obsługujący płynny zoom w oparciu o `Tick on-demand`.
- **`APlayerHUD`:** Odizolowany aktor HUD zarządzający widgetem `WBP_PlayerHUD`. Reaguje na eventy zmiany postaci (`OnPossessedPawnChanged`) i automatycznie subskrybuje pasek życia oraz statusy.

### Filar 2: System Obrażeń, Odrzutów i Kinetyki
- **`UDamageableComponent`:** 
  - Uniwersalny komponent zdrowia i wytrzymałości (`MaxDurability`, `InitialDurability`, `CurrentDurability`).
  - Automatyczne wiązanie zdarzeń upadku (`LandedDelegate` -> Fall Damage) oraz zderzeń ze ścianami w locie i przy ślizgu po ziemi oraz uderzeń w sufit (`OnComponentHit`).
  - Wykrywanie energii zderzenia na podstawie prędkości prostopadłej (`DotProduct` z normalną ściany).
  - `ApplyKineticImpact(float ImpactSpeed)`: Przelicza nadmiarową prędkość na obrażenia na bazie progu (`ImpactSpeedThreshold`) i mnożnika (`ImpactDamageMultiplier`).
- **`UKnockbackComponent`:**
  - Obsługa odrzutów dla postaci (przez `LaunchCharacter` z mnożnikiem `AirborneMultiplier`) oraz obiektów fizycznych (`AddImpulse`).
  - Wsparcie dla odporności na odrzut (`KnockbackResistance`), niewrażliwości (`bIsImmune`) oraz limitu prędkości (`MaxAllowedVelocity = 3500.0f`).
  - Własny enum `EKnockbackFalloff` (`Linear`, `Constant`).
- **`UKineticForceLibrary`:**
  - `ApplyExplosion`: Obrażenia i radialny odrzut z tłumieniem liniowym.
  - `ApplyDirectionalKnockback`: Kierunkowy odrzut z regulowanym podbiciem w górę (`VerticalLiftRatio`).
  - `ApplyVortexPull`: Wir przyciągający jednostki i obiekty do centrum.
  - **Wsparcie dla brył fizycznych Chaos:** Dodano funkcję `KineticHelpers::GetSimulatingPrimitive()`. Jeżeli uderzony aktor nie ma `UKnockbackComponent`, impuls fizyczny aplikowany jest bezpośrednio na komponent symulujący fizykę (`AddImpulse(..., true /* bVelChange */)`).
  - Rozszerzono zapytania kolizyjne o kanał `ECC_WorldDynamic` (umożliwiający miotanie wybuchami także propami).
- **Kinetic Impact Transfer (Transfer Pędu: Prop $\to$ Gracz):**
  - Zaimplementowany w `AInteractivePropBase::HandleImpactDamage`.
  - **Rozwiązanie problemu kapsuły CMC:** Ponieważ kapsuła gracza ma dla Chaosu nieskończoną masę, fizyczne propy odbijały się od gracza bez przekazania pędu.
  - **Filtr iloczynu skalarnego:** Oblicza efektywną prędkość natarcia propa w stronę postaci (`EffectivePropSpeed = Max(Dot(-HitNormal), Dot(ToTarget))`).
  - **Zabezpieczenie przed przypadkowym dotknięciem:** Gdy gracz wbiega w stojący prop, prędkość propa wynosi 0 $\to$ brak odrzutu. Gdy prop leci w stronę gracza $\ge 300\text{ cm/s}$ $\to$ aplikowany jest odrzut i obrażenia kinetyczne proporcjonalne do masy (`Mass / 50 kg`).
  - Działa dookólnie (360°): uderzenie w plecy, bok lub przód postaci.

### Filar 3: Reaktywny System Statusów Żywiołowych (Elements & Status Effects)
- **`StatusEffectDefinitions.h` w `Environment/Elements/Data` (Centralny Rejestr Danych):**
  - Wszystkie definicje cech statusów oraz reguł kombinacji żywiołów zebrane w jednym pliku konfiguracyjnym w strukturach C++:
    - `FStatusReactionRule`: `bConsumeIncomingStatus`, `bRemoveExistingStatus`, `BonusInstantDamage`, `ReactionTag`.\n    - `FStatusEffectDefinition`: `EffectType`, `bIsLiquid`, `NaturallyAllowedMaterials`, `BypassMaterialIfActive`, `DamagePerSecond`, `TickInterval`, `Reactions` map.
    - `FStatusEffectRegistry::GetDefinition(EStatusEffectType)`: Zwraca kartę definicji dla danego statusu w czasie $O(1)$.
- **`UStatusEffectComponent` w `Shared/Components`:**
  - Odpowiada wyłącznie za stan podmiotu: przechowywanie statusów w `TMap<EStatusEffectType, FActiveStatusEffectInstance>`, odliczanie czasu trwania, brak duplikatów i cykl życia.
  - Obrażenia okresowe DoT oraz interwał tyknięcia pobierane w sposób dynamiczny z `FStatusEffectRegistry::GetDefinition()`.
  - Architektura oparta na zdarzeniach (`OnStatusEffectApplied`, `OnStatusEffectRemoved`, `OnElementalReactionTriggered`).
  - Optymalizacja `Tick on-demand`: komponent tickuje tylko wtedy, gdy na celu znajduje się przynajmniej jeden aktywny status.
- **`UElementalChemistryLibrary` w `Environment/Elements/Utilities`:**
  - Dedykowany silnik chemii regułowej (Chemistry Engine).
  - Klasyfikacja i taksonomia powłok: `IsLiquidStatus` odpytuje rejestr (`bIsLiquid`).
  - Weryfikacja kompatybilności materiałowej (`CanMaterialReceiveStatus`) bazująca na `NaturallyAllowedMaterials` i `BypassMaterialIfActive`.
  - Ewaluacja reakcji żywiołowych zwracająca DTO `FElementalReactionResult` (`EvaluateReaction`):
    - **Faza 1 (Reguły zdefiniowane w karcie żywiołu):**
      - `Wet` + `Burning` $\rightarrow$ zgaszenie ognia, odparowanie wody (`Steam_Extinguish` / `Fire_Extinguished`).
      - `Oiled` + `Burning` $\rightarrow$ natychmiastowy zapłon oleju, obrażenia bonusowe (`Oil_Ignition`).
      - `Wet` + `Electrified` $\rightarrow$ szok przewodzący prąd (`Conductive_Shock`).
    - **Faza 2 (Reguła Powłok Płynnych - Liquid Displacement):**
      - Każdy przychodzący płyn (`IncomingDef.bIsLiquid`) wypiera poprzednio nałożony płyn (`ExistingStatusToRemove = PoprzedniPłyn`, `ReactionTag = Liquid_Displaced`), wykluczając jednoczesne posiadanie np. `Wet` i `Oiled`.

### Filar 4: Struktury Lochu, Tożsamość Materiałowa i Rekwizyty (Props)
- **`EPhysicalMaterialType` w `Shared/Enums`:**
  - Współdzielone przez cały świat: ściany, wazy, graczy i potwory (`Flesh`, `Stone`, `Wood`, `Metal`, `Glass`).
- **`AInteractivePropBase` w `Dungeon/Props/InteractivePropBase`:**
  - Klasa bazowa dla fizycznych elementów wyposażenia (skrzynki, wazy, beczki).
  - Posiada `UStaticMeshComponent` (Chaos Physics, CCD), `UDamageableComponent` oraz `UStatusEffectComponent`.
  - Obsługuje `IGrabbableInterface` (podnoszenie, rzucanie, sprawdzanie masy) oraz `IMaterialProviderInterface`.
  - Posiada parametry transferu kinetycznego: `bTransferKineticKnockback`, `MinImpactSpeedForKnockback`, `KnockbackStrengthMultiplier`.
- **`UInteractionComponent` w `Shared/Components`:**
  - Działa zarówno dla Gracza, jak i dla AI (używa `GetActorEyesViewPoint`).
  - Obsługuje chwytanie/rzucanie obiektów (`IGrabbableInterface`) oraz aktywację logiczną (`IInteractableInterface`).
- **`ADungeonStructureBase`:** 
  - Klasa bazowa dla podłóg, ścian, sufitów i filarów (`BlockAll`, brak narzutu fizyki Chaos).
  - Konfigurowalna niszczalność (`bIsDestructible = true/false`).
  - **Punch-Through (Przebijanie barykad):** Gdy uderzenie o ścianę przekracza jej HP i ją niszczy, wyłączana jest natychmiast kolizja, a pęd uderzającego obiektu/gracza jest przekazywany dalej za ścianę pomniejszony o opór (`PunchThroughVelocityRetention = 0.6f`).
- **`AVolatileProp` & `AExplosiveBarrelProp`:**
  - `AVolatileProp`: Niestabilne obiekty lochu (bomby, miny, bańki żywiołów, beczki). Konfigurowalny promień (`EffectRadius`), obrażenia (`BaseDamage`), odrzut (`KnockbackForce`), żywioł (`StatusToApply`) i czas trwania statusu (`StatusDuration`).
  - Blokada zadawania obrażeń otoczeniu w trakcie trzymania w rękach (`IsGrabbed() == true`).
- **`ASimpleSwitchProp`:**
  - Dźwignie, płyty naciskowe i przełączniki mechaniczne (`bIsActive`, `bCanBeUsed`, `OnSwitchToggled`).

### Filar 5: Warstwa Prezentacji i UI (HUD & Status Bar)
- **`UStatusEffectIconWidget`:** Klasa bazowa dla ikony aktywnego statusu (`WBP_StatusIcon`).
  - Posiada opcjonalne bindingi na `IconImage`, `DurationText` i `ProgressBar` roll.
  - Posiada mapę tekstur `StatusTextures` (`TMap<EStatusEffectType, TObjectPtr<UTexture2D>>`).
- **`UPlayerHUDWidget`:**
  - Dynamicznie spawnuje ikonki statusów w kontenerze `StatusEffectsContainer` (`HorizontalBox`).
  - Kontener ma ustawione `Auto Size = True`, dzięki czemu automatycznie dopasowuje szerokość i wysokość bez deformacji.

### Filar 6: Ujednolicony Standard Blueprintów (Custom|... & Tooltips)
- **Jednolity prefiks kategorii (`Custom|...`):**
  - Wszystkie autorskie zmienne i metody we wszystkich 10 plikach nagłówkowych projektu zostały przypisane do podkategorii z prefiksem `Custom|`:
    - `Custom|Durability`
    - `Custom|Kinetic`
    - `Custom|Status Effects` (oraz `Custom|Status Effects|Burning`, `Custom|Status Effects|Debug`)
    - `Custom|Material`
    - `Custom|Interaction`
    - `Custom|Camera`
    - `Custom|Input`
    - `Custom|Volatile` (oraz `Custom|Volatile|Explosion`, `Custom|Volatile|Status`)
    - `Custom|Switch`
    - `Custom|Destruction`
    - `Custom|Events`
    - `Custom|Components`
    - `Custom|Networking`
  - Wpisanie w filtr Details w edytorze Blueprintów słowa `custom` natychmiast wyświetla wyłącznie pola projektu, ukrywając setki wbudowanych opcji silnika.
- **Wyczerpujące komentarze C++ (Tooltips):**
  - Nad wszystkimi `UPROPERTY` i `UFUNCTION` dodano bloki dokumentacyjne `/** ... */`.
  - Po najechaniu kursorem na dowolne pole w edytorze pojawia się dymek z wyjaśnieniem działania, jednostkami (cm, cm/s, kg) oraz konsekwencjami w grze.
- **Standard formatowania podkategorii w C++:**
  - Unreal Header Tool (UHT) oraz parser Details dzielą ciąg ściśle po znaku `|`. Należy zawsze pisać `Category = "Custom|SubCategory"` (bez spacji wokół kreski), aby uniknąć osieroconych duplikatów kategorii.

### Filar 7: Odizolowana Domena Sieciowa (`Networking/`)
- **`UNetworkFunctionLibrary` & `NetUtils` namespace:**
  - Czysta, scentralizowana biblioteka pomocnicza dla sprawdzania autorytetu serwera i formatowania logów.
  - `NetUtils::HasAuthority(Context)` – bezbłędne, odporne na `nullptr` sprawdzenie autorytetu bez powtarzania `GetOwner()`.
  - `NetUtils::GetNetRolePrefix(Context)` – automatyczne rozpoznawanie kontekstu i zwracanie `[Server]`, `[Client]` lub `[Standalone]`.
  - Ekspozycja metod w grafach Blueprintów w kategorii `Custom|Networking`.

---

## 4. Wnioski, Diagnoza Silnika i Lekcje (Lessons Learned)

1. **Kolizja Kinematyczna Postaci vs Chaos Rigid Bodies:**
   - Kapsuła `ACharacter` nie posiada symulacji sztywnej bryły fizycznej. Bez dedykowanej warstwy w `AInteractivePropBase` lecące ciężkie obiekty tracą pęd przy kontakcie z graczem bez wywołania odrzutu.
   - Połączenie iloczynu skalarnego wektora ruchu propa z wywołaniem `UKnockbackComponent::ApplyImpulseForce` i `UDamageableComponent::ApplyKineticImpact` daje realistyczne, fizyczne odczucie impaktu.
2. **Skalowanie Odrzutu Masą:**
   - Przyjęty przelicznik `Mass / 50.0f` sprawdza się znakomicie dla obiektów w przedziale 20–200 kg. 
3. **Miejsca na Optymalizację:**
   - **Cap pędu:** Wprowadzenie górnego limitu transferowanej siły dla propów powyżej 500 kg (aby uniknąć wystrzelenia postaci poza geometrię poziomu).
   - **Feedback audiowizualny:** Dodanie screen shake kamery i dźwięku uderzenia zależnego od siły uderzenia propa w postać.

---

## 5. Najbliższy Kamień Milowy: Co-op Network Pass & Wielosobowy "Theme Park"

Strategia hybrydowa zakłada, że Theme Park będzie od pierwszego uruchomienia **poligonem sieciowym (Multiplayer Gym)** dla 2 graczy w PIE, testującym mechaniki w warunkach asynchronicznych.

### Krok 1: Przejście obecnych modułów na Co-op (Network Pass)
1. **`UDamageableComponent`:** Replikacja `CurrentDurability` (`OnRep_CurrentDurability`), autorytatywne naliczanie obrażeń na serwerze, refaktoryzacja z użyciem `NetUtils::HasAuthority(this)`.
2. **`UStatusEffectComponent`:** Zamiana `TMap` na replikowaną `TArray`, wprowadzenie wzorca *Zero-Bandwidth Timers* (`ServerEndTime`), autorytatywny DoT.
3. **`AInteractivePropBase` & `AVolatileProp`:** Włączenie `bReplicates = true`, `SetReplicateMovement(true)`, serwerowe wybuchy z lekkim multicastem FX.
4. **`UInteractionComponent`:** Server RPCs na podnoszenie (`Server_RequestGrab`), upuszczanie i rzucanie z wzorcem *Attach-on-Grab*.

### Krok 2: Wielosobowy "Theme Park" Fizyki, Propów i Żywiołów (PIE: 2 Players)
1. **Dynamiczne Popychacze / Ubijaki (Piston / Slapper Rig):**
   - Ruchome bryły/tłoki uderzające propy w stronę graczy – weryfikacja replikacji transferu pędu i braku jittera.
2. **Rampa / Zsyp Propów:**
   - Pochylnia do testowania toczenia się i uderzeń toczących się beczek w obu graczy.
3. **Stanowiska Wagowe (10 kg – 800 kg):**
   - Weryfikacja podnoszenia i rzucania propami przez graczy (kto pierwszy podniesie, brak desynchronizacji).
4. **Tor Reakcji Łańcuchowych i Żywiołów:**
   - Kaskadowe detonacje beczek wybuchowych i żywiołowych z perspektywy obu połączonych graczy.

---

## 6. Powiązane Procedury i Dokumentacje (SOP)
- **Standardy Architektury Sieciowej (Co-op 1–6 Graczy):**  
  👉 [`.context/CORE_COOP_PRINCIPLES.md`](file:///E:/UE_PROJECTS/MyProject/.context/CORE_COOP_PRINCIPLES.md)
- **Pipeline Graficzny i Konfiguracja Ikon Statusu:**  
  👉 [`.context/UI_STATUS_ICONS_PIPELINE.md`](file:///E:/UE_PROJECTS/MyProject/.context/UI_STATUS_ICONS_PIPELINE.md)

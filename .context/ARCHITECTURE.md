# Architektura Systemowa i Standardy Projektowe: Dungeon Crawler Co-op

Dokument stanowi **centralny i nadrzędny dokument architektoniczny projektu** *Dungeon Crawler* (Unreal Engine 5.8 C++, `MYPROJECT_API`).
Definiuje wzorce inżynierii oprogramowania, modularność domeny, prawa fizyki i kinetyki, silnik chemii żywiołów, autorytatywną warstwę sieciową Co-op (1–6 graczy) oraz specyfikację struktur lochu zgodną z [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md) i [CORE_COOP_PRINCIPLES.md](file:///E:/UE_PROJECTS/MyProject/.context/CORE_COOP_PRINCIPLES.md).

---

## 1. Paradygmat Architektoniczny (Java/Spring & Clean Architecture Standards)

Projekt implementuje rygorystyczne wzorce czystego kodu inspirowane wzorcami backendowymi (Java/Spring Boot, Event-Driven Architecture, REST/RPC):

* **Single Responsibility Principle (Kompozycja ponad Dziedziczenie):**
  * Klasy `AActor` oraz `ACharacter` pełnią wyłącznie rolę punktów styku / orkiestratorów (odpowiednik `@RestController`).
  * Wszelka logika domenowa (integralność fizyczna, chemia żywiołów, interakcje, mechanizmy) jest hermetyzowana w dedykowanych komponentach `UActorComponent` (odpowiednik `@Service`).
* **Dependency Inversion & Loose Coupling (Architektura Interfejsowa):**
  * Komunikacja międzydomenowa i manipulacja obiektami w świecie gry odbywa się **wyłącznie za pośrednictwem interfejsów `UInterface` / `IInterface`**.
  * Całkowity zakaz twardego rzutowania (`Cast<T>`) w kodzie domenowym.
  * Kluczowe kontrakty projektu:
    - [`IMaterialProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/MaterialProviderInterface.h) – tożsamość materiałowa (`Stone`, `Wood`, `Metal`, `Glass`, `Flesh`).
    - [`IInteractableInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/IInteractableInterface.h) – obsługa interakcji klawiszem `E` / AI.
    - [`IGrabbableInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/IGrabbableInterface.h) – kontrakt manipulacji propami fizycznymi.
    - [`IMechanismReceiverInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/MechanismReceiverInterface.h) – odbiór sygnałów logicznych ON/OFF z przełączników i płyt naciskowych.
    - [`ICarryAnchorProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h) – odseparowanie logiki trzymania propa od konkretnej klasy postaci.
    - [`IStatProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/StatProviderInterface.h) – ujednolicone zapytania o stan HP/durability dla UI.
* **Architektura Sterowana Zdarzeniami (Event-Driven Architecture):**
  * Komunikacja komponent -> UI / Prezentacja / Efekty VFX realizowana jest przez dynamiczne delegaty multicastowe (`DECLARE_DYNAMIC_MULTICAST_DELEGATE` – odpowiednik Spring Application Events).
* **Ścisła Separacja Domeny od Prezentacji (MVC / Layered Pattern):**
  * **C++ (Backend / Domena):** Czysta logika biznesowa, matematyka kinetyczna, autorytatywny kod sieciowy, ewaluacja chemiczna, struktury `USTRUCT`, komponenty i interfejsy.
  * **Blueprints (Frontend / Widok / Prefaby):** Wyłącznie klasy pochodne służące do spinania assetów wizualnych (Static Meshe, animacje, materiały, dźwięki, widgety). Obowiązuje zakaz programowania logiki biznesowej w Blueprintach.
* **Data-Driven Configuration:**
  * Parametry fizyczne, mnożniki i progi definiowane są w strukturach konfiguracyjnych (`UPROPERTY(EditDefaultsOnly)` – odpowiednik `@ConfigurationProperties`), eliminując sztywne if-ologie.

---

## 2. Mapa Architektury Kodu (`Source/MyProject/`)

```text
Source/MyProject/
├── Networking/                                     <-- Warstwa autorytatywna Co-op (1–6 graczy)
│   └── NetworkFunctionLibrary.h/.cpp               (Makra REQUIRE_AUTHORITY, ConfigurePhysicsReplication, AttachCarriedProp)
│
├── Dungeon/                                        <-- Świat lochu, struktury i mechanizmy
│   ├── Structure/
│   │   └── DungeonStructureBase.h/.cpp             (Modularne ściany/podłogi, Punch-Through, niszczalność, replikacja)
│   ├── Props/
│   │   ├── InteractivePropBase/                    (Fizyczne rekwizyty Chaos, kwantyzacja transformu, transfer kinetyczny)
│   │   ├── VolatileProp/                           (Niestabilne obiekty alchemiczne, wybuchy autorytatywne, NetMulticast FX)
│   │   ├── SwitchPropBase/                         (Abstrakcyjny przełącznik/aktywator logiczny, bAllowSwitchBack, TargetMechanisms)
│   │   ├── SimpleSwitchProp/                       (Dźwignia/przełącznik ścienny z IInteractableInterface)
│   │   └── PressurePlateProp/                      (Fizyczna płyta naciskowa sumująca rzeczywistą masę ciał >= 50 kg)
│   └── Mechanisms/
│       ├── MechanismTrapBase/                      (Abstrakcyjna baza pułapek z pętlą czasową bIsContinuousLoop i IMechanismReceiver)
│       └── PistonTrap/                             (Kamienny taran/tłok ścienny/podłogowy, maszyna stanów EPistonState, Zero-Tick)
│
├── Environment/                                    <-- Fizyka, kinetyka i żywioły
│   ├── Kinetic/
│   │   ├── Components/KnockbackComponent/          (Aplikowanie odrzutów dla postaci i impulsów dla ciał sztywnych)
│   │   ├── Utilities/KineticForceLibrary           (Radialne eksplozje, wiry kinetyczne, impulsy Chaosu)
│   │   └── Enums/KineticEnums.h                    (EKnockbackFalloff)
│   └── Elements/
│       ├── Data/StatusEffectDefinitions.h          (Centralny rejestr FStatusEffectRegistry, parametry DoT i reakcji)
│       ├── Enums/ElementEnums.h                    (EStatusEffectType: None, Burning, Wet, Electrified, Oiled)
│       ├── StatusZone/ElementalStatusZone          (Autonomiczne strefy rozlewisk/kałuż/ognia, LoS, wygaszanie konfliktów cieczy)
│       └── Utilities/
│           ├── ElementalChemistryLibrary           (Silnik reakcji chemicznych i kompatybilności materiałowej)
│           └── ElementalDeliveryLibrary            (Point Hit, Surface Splash, Radial Burst z LoS, Status Zone)
│
├── Shared/                                         <-- Współdzielone serwisy, komponenty i kontrakty
│   ├── Components/
│   │   ├── DamageableComponent/                    (Replikowane durability/HP, Server-Authoritative, kinetic debounce)
│   │   ├── InteractionComponent/                   (Kinematic Sweep Follow, Carry State Machine, Anti-Bulldozer, Swing Throw)
│   │   └── StatusEffectComponent/                  (Replikowany zarządca statusów, Zero-Bandwidth Timers, Elemental Priority Pipeline)
│   ├── Interfaces/
│   │   ├── CarryAnchorProviderInterface.h          (Kontrakt kotwicy rąk i limitów pchania dla postaci niosącej)
│   │   ├── IGrabbableInterface.h                   (Kontrakt chwytania i rzucania fizycznymi propami)
│   │   ├── IInteractableInterface.h                (Kontrakt interakcji klawiszem E)
│   │   ├── MaterialProviderInterface.h             (Zapytanie o tożsamość materiałową EPhysicalMaterialType)
│   │   ├── MechanismReceiverInterface.h            (Kontrakt odbiornika sygnałów aktywatorów SetMechanismState)
│   │   └── StatProviderInterface.h                 (Kontrakt na odczyt wskaźników HP/durability)
│   └── Enums/
│       └── PhysicalMaterialEnums.h                 (EPhysicalMaterialType: Stone, Wood, Metal, Glass, Flesh)
│
├── Player/                                         <-- Postać gracza i sterowanie
│   ├── Components/PlayerCameraComponent/           (Płynny zoom TPP/Top-Down, On-Demand Tick)
│   ├── PlayerCharacter.h/.cpp                      (Kinematyczna postać CMC, Flesh, MoveBlockedBy fizyczne pchanie)
│   └── PlayerCharacterController.h/.cpp
│
└── UI/                                             <-- Warstwa prezentacji stanu gry
    ├── PlayerHUD/                                  (Aktor HUD orkiestrujący widgety)
    ├── PlayerHUDWidget/                            (Główny widok: pasek zdrowia + kontener statusów)
    ├── StatusIconWidget/                           (Dynamiczna kontrolka ikony statusu z radialnym timerem)
    └── StatBarWidget.h/.cpp                        (Wskaźnik paskowy HP/durability)
```

---

## 3. Żelazna Zasada Ekosystemu: Święta Trójca Propów i Struktur

Zgodnie ze specyfikacją [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md), **żaden interaktywny ani fizyczny obiekt w świecie gry nie może być "pustym Static Meshem"**. Wszystkie elementy (propy, struktury, pułapki, barykady) bezwzględnie implementują **Świętą Trójcę**:

```mermaid
classDiagram
    class HolyTrinityObject {
        <<Contract>>
    }
    class IMaterialProviderInterface {
        +GetMaterialType() EPhysicalMaterialType
    }
    class UDamageableComponent {
        +CurrentDurability : float
        +ApplyDamage(Amount)
        +ApplyKineticImpact(ImpactSpeed)
    }
    class UStatusEffectComponent {
        +ActiveStatusEffects : TArray
        +ApplyStatus(NewStatus, Duration)
        +HasStatus(Status) bool
    }

    HolyTrinityObject ..|> IMaterialProviderInterface : 1. Tożsamość Materiałowa
    HolyTrinityObject *-- UDamageableComponent : 2. Wytrzymałość i Życie
    HolyTrinityObject *-- UStatusEffectComponent : 3. Reaktywność Żywiołowa
```

1. **Tożsamość Materiałowa ([`IMaterialProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/MaterialProviderInterface.h)):**
   - Określa naturę fizyczną obiektu: `Stone`, `Wood`, `Metal`, `Glass`, `Flesh`.
   - Determinuje reakcje żywiołowe (np. kamień i metal odrzucają ogień, drewno płonie, szkło jest kruche).
2. **Wytrzymałość i Życie ([`UDamageableComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/DamageableComponent/DamageableComponent.h)):**
   - Autorytatywne punkty wytrzymałości (`CurrentDurability`, `MaxDurability`).
   - Kinetyczne obrażenia od prędkości kolizji (`ApplyKineticImpact`) z debouncem przeciw wielokrotnym trafieniom (`KineticImpactCooldown = 0.25s`).
3. **Reaktywność Żywiołowa ([`UStatusEffectComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h)):**
   - Obsługa powłok (`Wet`, `Oiled`, `Burning`, `Electrified`).
   - Autorytatywne DoT, reakcje chemiczne i zoptymalizowana sieć *Zero-Bandwidth Timers*.

---

## 4. Architektura Postaci, Kinetyki i Trzymania Obiektów

### 4.1. Kinematyczna Postać Gracza ([`APlayerCharacter`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Player/PlayerCharacter.h))
- Oparta na stabilnym, sieciowym `CharacterMovementComponent` (CMC) z wyłączonym zbędnym tickiem (`bCanEverTick = false`).
- Postać nie symuluje fizyki jako Rigid Body w Chaosie, co eliminuje wystrzeliwanie postaci przy kolizjach ze skrzyniami i podłożem.
- **Fizyczne Pchanie Ciałem (`MoveBlockedBy`):**
  - Gracz posiada zdefiniowaną masę wirtualną ($100\text{ kg}$) i siłę naporu ($150\,000\text{ N}$).
  - Kolizja z propem $\le 100\text{ kg}$ przekazuje wektorową siłę pchania, pozwalając na toczenie głazów i przepychanie skrzyń.
  - Kolizja z propem $> 100\text{ kg}$ traktowana jest jak solidna ściana.

### 4.2. Wzorzec Kinematycznego Prowadzenia Propów (Kinematic Sweep Shield)
W [UInteractionComponent](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h) całkowicie odrzucono sztywny `AttachToComponent` oraz niestabilny w sieci `PhysicsHandleComponent`:

```mermaid
flowchart TD
    A["Gracz wciska E / Trzyma Prop"] --> B["UInteractionComponent (On-Demand Tick)"]
    B --> C["ICarryAnchorProviderInterface::GetHoldAnchorComponent()"]
    C --> D["Kinematic Sweep w stronę kotwicy (bSweep = true)"]
    D --> E{"Kolizja w locie (SweepHit)?"}
    E -- "Trafiono przeszkodę <= 100kg" --> F["Aplikacja PlayerPushForce (Pchanie skrzyni tarczą)"]
    E -- "Trafiono przeszkodę > 100kg" --> G["Anti-Bulldozer Clamp (Zerowanie prędkości Chaosu)"]
    D --> H{"Dystans rąk > CarryBreakDistance (70 cm)?"}
    H -- "Tak (Zablokowanie o ścianę)" --> I["Carry Grip Break (Upuszczenie propa pod nogi)"]
    H -- "Nie" --> J["Prop prowadzony stabilnie jako Tarcza Blokująca"]
```

- **Rola Tarczy (Blocking Shield):** Trzymana deska lub głaz blokuje lecące pociski (`ECC_WorldDynamic`), magię i uderzenia wrogów, pochłaniając energię i chroniąc gracza.
- **Decoupling przez Interfejs:** Komponent interakcji nie zależy od `APlayerCharacter`, lecz od lekkiego interfejsu [`ICarryAnchorProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h).
- **Rzut Zamachem Myszką (Camera Angular Swing Throw) i Pure Drop:**
  - Wciśnięcie `E` w spoczynku upuszcza prop pod nogi z zerową prędkością (czysty spadek grawitacyjny).
  - Dynamiczny obrót kamerą (zamach myszą) wylicza prędkość kątową na ramieniu trzymania i nadaje pęd po łuku zamachu.
  - LPM wykonuje dedykowany silny rzut na wprost z pełną walidacją serwera.

---

## 5. Silnik Żywiołów, Chemii i Dystrybucji (Elemental Engine)

### 5.1. Hierarchia Reakcji Chemii (Elemental Priority Pipeline)
Podczas aplikacji statusu przez [`UStatusEffectComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h) zachodzi dwufazowa ewaluacja logiczna:
1. **Faza 1 (Żywioł vs Aktywna Powłoka):**
   - Jeśli cel ma aktywny status (np. `Wet`), a przychodzi ogień (`Burning`), wyzwalana jest reakcja `Steam_Extinguish`. Woda odparowuje, ogień zostaje ugaszony (`bConsumeIncomingStatus = true`), nie dotykając materiału pod spodem.
   - Jeśli cel jest `Oiled`, a przychodzi `Burning` $\rightarrow$ gwałtowna detonacja `Oil_Ignition`.
   - Jeśli cel jest `Wet`, a przychodzi `Electrified` $\rightarrow$ natychmiastowe porażenie przewodzące `Conductive_Shock`.
2. **Faza 2 (Żywioł vs Tożsamość Materiałowa):**
   - Dopiero gdy żywioł nie zostanie zneutralizowany przez powłokę, silnik sprawdza [`UElementalChemistryLibrary::CanMaterialReceiveStatus`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h). Kamień i metal odrzucają ogień, uniemożliwiając ich zapalenie.

### 5.2. Architektura Dystrybucji Żywiołów ([`UElementalDeliveryLibrary`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Elements/Utilities/ElementalDeliveryLibrary.h))
Silnik obsługuje 4 fundamentalne archetypy dostarczania:
1. **Point Hit:** Punktowe uderzenie pojedynczym pociskiem w 1 aktora.
2. **Surface Splash:** Płaski rozbryzg z fiolki/naczynia na konkretną płaszczyznę ze sprawdzeniem półprzestrzeni (Half-Space check).
3. **Radial Burst:** Sferyczna fala uderzeniowa z testem widoczności Line-of-Sight (`ECC_Visibility`), zapobiegającym przenikaniu efektów przez ściany i zamknięte wrota.
4. **Status Zone ([`AElementalStatusZone`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Elements/StatusZone/ElementalStatusZone.h)):** Trwałe pole w świecie (kałuża, plama oleju, pożar):
   - Dynamiczny obrys LoS (`RebuildPerimeterPoints`) dopasowujący krawędź cieczy do geometrii ścian.
   - Rozwiązywanie konfliktów płynów: nowszy płyn nadpisuje stary w strefie nakładania (eliminacja flickeringu).
   - Ograniczenia wysokości: ciecze posiadają próg `LiquidSurfaceHeight = 35 cm`, a ogień `FireSurfaceHeight = 85 cm`.

---

## 6. Architektura Mechanizmów i Pułapek Lochu (Dungeon Mechanisms)

Wprowadzona w ramach realizacji [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md):

```mermaid
classDiagram
    class IMechanismReceiverInterface {
        <<Interface>>
        +SetMechanismState(bActive, TriggeringActor)
    }

    class ASwitchPropBase {
        <<Abstract>>
        #MeshComponent : UStaticMeshComponent
        #DamageableComponent : UDamageableComponent
        #StatusEffectComponent : UStatusEffectComponent
        +TargetMechanisms : TArray~AActor~
        +bAllowSwitchBack : bool
        +SetActiveState(bNewState, TriggeringActor)
    }

    class ASimpleSwitchProp {
        +Interact(Interactor)
    }

    class APressurePlateProp {
        #PlateMesh : UStaticMeshComponent
        #TriggerBox : UBoxComponent
        +RequiredMass : float
        +RecalculateMassAndEvaluate()
    }

    class AMechanismTrapBase {
        <<Abstract>>
        #BaseMeshComponent : UStaticMeshComponent
        +bIsContinuousLoop : bool
        +LoopInterval : float
        +SetTrapActive(bNewActive, TriggeringActor)
        +TriggerTrap(TriggeringActor)
        #ExecuteTrapAction(TriggeringActor)*
    }

    class APistonTrap {
        -PistonHeadMesh : UStaticMeshComponent
        -DamageBox : UBoxComponent
        -PistonState : EPistonState
        +PushDirection : FVector
        +StrokeDistance : float
        +ExtendSpeed : float
        +KnockbackSpeed : float
    }

    ASwitchPropBase <|-- ASimpleSwitchProp
    ASwitchPropBase <|-- APressurePlateProp
    AMechanismTrapBase <|-- APistonTrap
    AMechanismTrapBase ..|> IMechanismReceiverInterface
    ASwitchPropBase ..> IMechanismReceiverInterface : Powiadamia TargetMechanisms
```

### 6.1. Aktywatory i Przełączniki ([`ASwitchPropBase`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Props/SwitchPropBase/SwitchPropBase.h))
- **`ASimpleSwitchProp`:** Ścienna wajcha/dźwignia aktywowana klawiszem `E` przez [`IInteractableInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/IInteractableInterface.h).
- **`APressurePlateProp`:** Podłogowa płyta naciskowa reagująca na **rzeczywistą masę fizyczną** (`RequiredMass = 50.0 kg`). Rejestruje nachodzące postacie ($80\text{ kg}$) oraz propa fizyczne (skrzynia $60\text{ kg}$, głaz $70\text{ kg}$), płynnie zapadając się w posadzkę (`Tick On-Demand`).
- Obsługa przełączników jedno- i dwukierunkowych (`bAllowSwitchBack`), powiadamianie celów implementujących [`IMechanismReceiverInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/MechanismReceiverInterface.h).

### 6.2. Pułapki i Tarany ([`AMechanismTrapBase`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Mechanisms/MechanismTrapBase/MechanismTrapBase.h))
- **`APistonTrap`:** Kamienny taran ścienny / zgniatacz sufitowy / katapulta podłogowa.
  - Maszyna stanów `EPistonState` (`IdleAtHome`, `Extending`, `HoldingAtExtended`, `Retracting`).
  - Optymalizacja Zero-Tick: `Tick()` aktywny wyłącznie podczas fizycznego ruchu głowicy taranu.
  - Błyskawiczny suw w przód ($1200\text{ cm/s}$), opóźnienie w wysunięciu, powolne cofanie ($150\text{ cm/s}$).
  - Aplikuje odrzut kinetyczny na graczy (`LaunchCharacter`), pęd na obiekty fizyczne (`AddImpulse`) i niszczy zniszczalne barykady.
- **`AProjectileLauncherTrap`:** Ścienna paszcza miotająca kamieniami/pociskami (poligon pod testy tarczy `BP_PlankShield_Wood`).

---

## 7. Architektura Sieciowa Co-op (Server-Authoritative Co-op)

Szczegółowo zdefiniowana w [CORE_COOP_PRINCIPLES.md](file:///E:/UE_PROJECTS/MyProject/.context/CORE_COOP_PRINCIPLES.md):

1. **Server-Authoritative First:**
   - Wszelka mutacja stanu gry chroniona jest makrem strażniczym `REQUIRE_AUTHORITY()`.
   - Obrażenia, reakcje chemiczne, pchanie propów, wyzwalanie mechanizmów i rzuty kalkulowane są wyłącznie na serwerze.
2. **Wzorzec Zero-Bandwidth Timers:**
   - Zamiast replikowania upływu czasu co klatkę, serwer replikuje jednorazowo `ServerEndTime`. Klienci lokalnie obliczają czas pozostały do wygaśnięcia efektu (`LocalRemaining = FMath::Max(0.0f, ServerEndTime - WorldTime)`), co redukuje zużycie pasma podczas trwania efektów do **0 bajtów/s**.
3. **Kwantyzacja i Kompresja Transformów:**
   - Rekwizyty [`AInteractivePropBase`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h) posiadają kwantyzację: `RoundTwoDecimals` dla pozycji i `ByteComponents` (1 bajt per oś) dla rotacji.
   - Propy w spoczynku przechodzą w stan uśpienia sieciowego (`NetDormancy = DORM_DormantAll`).
4. **RPCs z Walidacją:**
   - `Server_RequestGrab`, `Server_RequestForwardThrow`, `Server_RequestDropOrSwing`, `Server_RequestInteract` posiadają pełną walidację `_Validate` (weryfikacja odległości, stanu gracza i clamp prędkości).

---

## 8. Standard Siatki Modularnej i Poziomu Theme Park (Loch v0.1)

Zgodnie z [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md) architektura geometrii lochu opiera się na metrycznej siatce modularnej:

| Element | Wymiary (X × Y × Z) | Rola w grze i uzasadnienie |
| :--- | :--- | :--- |
| **Klocek Podłogi / Sufitu** | **`400 × 400 × 20 cm`** | Podstawowy kafelek (4x4m), swobodny bieg do 3 graczy obok siebie. |
| **Klocek Ściany Pełnej** | **`400 × 30 × 350 cm`** | Grubość 30 cm zapobiega clippingowi kamer TPP i artefaktom Lumena. |
| **Wnęka Drzwiowa / Brama** | **`200 × 30 × 280 cm`** | Umożliwia przenoszenie szerokich skrzyń i rzucanie głazami. |
| **Klocek Kolumny / Narożnika**| **`40 × 40 × 350 cm`** | Maskowanie łączeń ścian i punkty oparcia sklepień. |

> **Zasada Pivot Point:** Wszystkie kafelki posiadają punkt bazowy wycentrowany w osi XY na krawędzi modułu lub w dolnym rogu na poziomie $Z = 0$, co gwarantuje natychmiastowe przyciąganie do siatki edytora (`Grid Snap = 50 / 100 cm`).

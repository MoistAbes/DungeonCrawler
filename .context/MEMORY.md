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
│   ├── NetworkFunctionLibrary.h/.cpp              (Makra REQUIRE_AUTHORITY, SetupQuantizedPhysicsReplication, AttachCarriedProp, DetachCarriedProp)
│
├── Dungeon/                                       <-- Zawartość i architektura lochu
│   ├── Structure/
│   │   └── DungeonStructureBase.h/.cpp            (Modularne ściany, niszczalne moduły, Punch-Through, SetReplicates)
│   └── Props/
│       ├── InteractivePropBase/                   (Replikowane rekwizyty fizyczne, kwantyzacja transformu, Kinetic Transfer na serwerze)
│       ├── VolatileProp/                          (Niestabilne obiekty: Server-Authoritative wybuchy, DoT, NetMulticast FX)
│       ├── ExplosiveBarrelProp/                   (Wybuchające beczki - specjalizacja AVolatileProp)
│       └── SimpleSwitchProp/                      (Dźwignie, przełączniki, mechanizmy)
│
├── Shared/                                        <-- Uniwersalne mechaniki, komponenty i kontrakty
│   ├── Components/
│   │   ├── DamageableComponent/                   (Replikowany komponent zdrowia/durability, REQUIRE_AUTHORITY(), OnRep)
│   │   ├── InteractionComponent/                  (Wykrywanie raycastem, Server RPCs: chwytanie fizyczne i interakcja logiczna)
│   │   └── StatusEffectComponent/                 (Replikowany komponent statusów, Zero-Bandwidth Timers, Server-DoT)
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
  - Posiada tożsamość materiałową `IMaterialProviderInterface` (`EPhysicalMaterialType::Flesh`).
  - Korzysta z `UInteractionComponent`, `UDamageableComponent`, `UKnockbackComponent`, `UStatusEffectComponent`.

### Filar 2: System Obrażeń, Odrzutów i Kinetyki
- **`UDamageableComponent`:** 
  - Replikacja `CurrentDurability` oraz `MaxDurability` (`OnRep_...`).
  - Zabezpieczenie makrem `REQUIRE_AUTHORITY()`. Zweryfikowane w PIE (2 graczy, Listen Server) – obrażenia i upadki kalkulowane w 100% autorytatywnie na serwerze i replikowane do klienta.
- **`UKnockbackComponent`:**
  - Obsługa odrzutów dla postaci (`LaunchCharacter`) oraz obiektów fizycznych (`AddImpulse`).
- **`UKineticForceLibrary`:**
  - Radialne wybuchy, odrzuty, impulsy pędu Chaos.

### Filar 3: Reaktywny System Statusów Żywiołowych (Elements & Status Effects)
- **`StatusEffectDefinitions.h`:** Rejestr `FStatusEffectRegistry`.
- **`UStatusEffectComponent`:**
  - Replikowany komponent (`SetIsReplicatedByDefault(true)`).
  - Replikowana tablica `ActiveStatusEffects` z diffingiem w `OnRep_ActiveStatusEffects`.
  - Wzorzec *Zero-Bandwidth Timers* (`ServerEndTime`).

### Filar 4: Struktury Lochu, Tożsamość Materiałowa i Rekwizyty (Props)
- **`AInteractivePropBase`:**
  - Replikacja fizyki i spoczynku przez `NetUtils::SetupQuantizedPhysicsReplication(this)`.
  - Replikowana flaga trzymania `bIsBeingCarried` (`OnRep_IsBeingCarried`).
  - Zabezpieczenie `REQUIRE_AUTHORITY()` w kolizjach i transferze kinetycznym.
- **`AVolatileProp`:**
  - Server-Authoritative wybuchy i DoT.
  - Zdarzeniowy `Multicast_PlayExplosionEffects(FVector DetonationCenter)` dla wizualizacji i dźwięków.
- **`ADungeonStructureBase`:** 
  - Replikacja cyklu życia aktora (`SetReplicates(true)`), autorytatywne zniszczenie i Punch-Through.
- **`UInteractionComponent`:**
  - Wzorzec Client-Request -> Server-Execute.
  - RPCs: `Server_RequestGrab`, `Server_RequestReleaseOrThrow`, `Server_RequestInteract`.
  - Replikowane podpinanie i odpinanie propów przez `NetUtils::AttachCarriedProp` i `NetUtils::DetachCarriedProp` – bez jittera i desynchronizacji.

---

## 4. Stan Kamieni Milowych

### Krok 1: Przejście modułów na Co-op (Network Pass)
1. ✅ **`UDamageableComponent`:** Replikacja `CurrentDurability`, autorytatywne obrażenia (Zweryfikowane).
2. ✅ **`UStatusEffectComponent`:** Replikowana tablica, *Zero-Bandwidth Timers*, Server DoT (Zweryfikowane).
3. ✅ **`AInteractivePropBase` & `AVolatileProp`:** Replikacja ruchu, kwantyzacja transformu, Server Detonation + NetMulticast FX (Zweryfikowane).
4. ✅ **`ADungeonStructureBase`:** Replikacja usunięcia niszczalnych barykad (Zweryfikowane po włączeniu Replicates w Blueprint).
5. ✅ **`UInteractionComponent`:** Replikowane podnoszenie i rzucanie propami (Server RPCs + Attach-on-Grab).

# Project Memory: Rogue-like Dungeon Crawler (Co-op 1-4)

- Always follow architectural guidelines defined in `ARCHITECTURE.md`.

## Tech Stack
- Unreal Engine 5.x C++ (Chaos Physics, Enhanced Input, UMG, Server-Authoritative Networking)
- JetBrains Rider
- Pure Component-Based Architecture (Spring/Java Enterprise Style)

---

## Zaimplementowane Moduły i Komponenty

### 1. Gracz Fizyczny (`APlayerCharacter : public APawn`)
- **Architektura Ciała Sztywnego (Rigid Body Chaos Pawn):**
  - Pełna symulacja Chaos Physics na roocie (`CapsuleComponent->SetSimulatePhysics(true)`, `Mass = 80.0 kg`, `bUseCCD = true`).
  - Wymiary kapsuły: całkowity wzrost **180 cm** (`CapsuleHalfHeight = 90.0 cm`, `CapsuleRadius = 35.0 cm`).
  - Trwała blokada osi rotacji (`bLockXRotation = true`, `bLockYRotation = true`, `bLockZRotation = true` oraz `AngularDamping = 100.0f` + `RecreatePhysicsState()`) – postać zawsze stabilnie trzyma pion.
  - **Naturalna grawitacja ($Z$):** Minimalny damping liniowy (`LinearDamping = 0.01f`), co zapewnia pełne, ciężkie i naturalne przyspieszenie grawitacyjne ($9.8\text{ m/s}^2$) przy starcie gry, skokach i spadaniu.
- **Sterowanie Event-Driven (Zero stałego Tick CPU):**
  - **Wektorowy napęd różnicowy prędkości (Velocity Drive):** `Move()` wyliczający wektor siły przyspieszenia na podstawie błędu prędkości: $F = (V_{\text{target}} - V_{\text{current}}) \cdot \text{Responsiveness}$.
  - **Dedykowane hamowanie poziome:** `MoveCompleted()` zeruje wyłącznie prędkość w płaszczyźnie $XY$ przy puszczeniu klawiszy, nie wpływając na prędkość spadania w osi $Z$.
  - **Blokada napędu w powietrzu:** W locie (`!IsGrounded()`) sterowanie WASD jest wyłączone – trajektoria lotu jest w 100% fizyczną parabolą balistyczną z zachowaniem pędu.
  - **Wielokierunkowa detekcja podłoża (Sphere Sweep):** `IsGrounded()` wykorzystuje sferyczny sweep 3D (`SweepSingleByChannel` o promieniu podstawy kapsuły), eliminując błąd utraty kontroli na krawędziach platform oraz filtrując pionowe ściany (`ImpactNormal.Z > 0.5f`).
- **System Kamery (FPP / TPP Orbit):**
  - Punkt zaczepienia ramienia (`SpringArmComponent`) podniesiony na poziom oczu/głowy (`BaseEyeHeightOffset = 65.0f`).
  - Leniwy `Tick()` do płynnego zoomu kamery (załączany wyłącznie podczas przewijania kółka myszy i wyłączany zaraz po osiągnięciu docelowej odległości).
- **Obsługa kinetyki i zderzeń:**
  - Dynamiczny delegat `HandleCapsuleHit` wyliczający rzeczywistą prędkość względną zderzenia (`ImpactSpeed`) i aplikujący obrażenia kinetyczne do `UDamageableComponent`.

### 2. Wytrzymałość i Obrażenia (`UDamageableComponent`)
- Reużywalny serwis domenowy HP/Durability (`bCanEverTick = false`).
- Implementacja kontraktu `IStatProviderInterface` (dla pasków HUD).
- Ujednolicony algorytm kinetyczny `ApplyKineticImpact(float ImpactSpeed)` oparty na prędkości zderzenia ($v > \text{Threshold}$).
- Zdarzenia domenowe (Spring Events): `OnHealthChanged`, `OnDurabilityChanged`, `OnDestroyed`.

### 3. Interakcja i Rekwizyty Fizyczne
- `UInteractionComponent`: Raycasting w oparciu o kamerę (`GetPlayerViewPoint` / crosshair), podnoszenie i rzucanie fizycznymi aktorami za pomocą `UPhysicsHandleComponent` z buforem ignorowania kolizji rzucającego gracza (`AActor*`).
- `IInteractableInterface`: Kontrakt biznesowy (`Interact`, `CanInteract`, `CanGrab`, `OnGrabbed`, `OnDropped`).
- `AInteractivePropBase`: Baza obiektów fizycznych z dynamicznym obliczaniem prędkości względnej kolizji (`DotProduct(RelativeVelocity, ImpactNormal)`).
- Zabezpieczenia geometrii Chaos: `bUseCCD = true`, `AngularDamping = 5.0f`, `LinearDamping = 0.8f`.

### 4. Warstwa Prezentacji (UMG / MVC)
- `UStatBarWidget` (`WBP_StatBar`): Generyczny presenter pojedynczego paska postępu (`StatProgressBar`).
- `UPlayerHUDWidget` (`WBP_PlayerHUD`): Kompozyt HUD subskrybujący zdarzenia z `UDamageableComponent`.

---

## Znane Problemy i Punkty Uwagi (Active Issues)

1. **Globalna Fizyka Chaos:**
    - Skonfigurowano substepping i limity w `DefaultEngine.ini` (`PhysicsSettings: MaxDepenetrationVelocity=100.0`, `MaxAngularVelocity=360.0`, `Substepping=True`).

---

## Backlog / Kolejne Kroki

- [ ] Test fizycznego ruchu gracza, skakania i interakcji z propami o małej i dużej masie.
- [ ] Implementacja stanów żywiołowych (`UElementalStateComponent` – Wet, Flammable, Burning, Conductive).
- [ ] Sieciowa replikacja chwytania i rzucania propami (`ServerRpc_Grab`, `ServerRpc_Throw`).
- [ ] Efekty zniszczenia obiektów (spawnowanie szczątków / Geometry Collection / Chaos Destruction).

---

## Decisions & Conventions Log

- 2026-08-29: Replikacja sieciowa włączona od etapu 0 dla każdego bytu domenowego.
- 2026-08-29: Brak logiki biznesowej w Blueprintach; Blueprinty wyłącznie jako prefab/widok.
- 2026-08-30: Transformacja postaci gracza z kinematycznego `ACharacter` na w 100% symulowany fizycznie `APawn` (`APlayerCharacter`) oparty w całości na zdarzeniach (Event-Driven Enhanced Input + Chaos Physics Solver).
- 2026-08-31: Refaktoryzacja `APlayerCharacter` – czyste forward-deklaracje, usunięcie martwych zmiennych, Sphere Sweep ground check na krawędziach, naturalna grawitacja bez dławienia $Z$ oraz pozycjonowanie kamery na wysokości oczu (180 cm humanoid).

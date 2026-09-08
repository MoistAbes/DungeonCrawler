# Przegląd Techniczny Projektu (Project Review & Audit)

Dokument stanowi **kompletną kartę audytu technicznego i architektonicznego** projektu *Dungeon Crawler*.
Zawiera szczegółową analizę 22 punktów review (od krytycznych `P0` po długoterminowe `P2` i mocne strony), ze statusem wdrożenia w bieżącym kodzie oraz planem działań.

---

## 📊 Podsumowanie Oceny i Macierz Statusu

| # | Punkt Audytu | Priorytet | Obszar | Status Faktyczny w Kodzie |
| :-: | :--- | :---: | :--- | :---: |
| **1** | Brak realnej walidacji `Server_RequestReleaseOrThrow` | 🔴 **P0** | Multiplayer / Interakcja | **[x] Rozwiązane** |
| **2** | `Server_RequestGrab_Validate()` niczego nie sprawdza | 🔴 **P0** | Multiplayer / Walidacja | **[x] Rozwiązane** |
| **3** | `PrimaryInteract()` ustawia `GrabbedActor` lokalnie przed RPC | 🔴 **P0** | Multiplayer / Desync | **[x] Rozwiązane** |
| **4** | Brakuje formalnego state machine dla grab/carry | 🟠 **P1** | Architektura Stanu | **[x] Rozwiązane** |
| **5** | Carry transform jest wykonywany lokalnie | 🟠 **P1** | Multiplayer / Prezentacja | **[~] Częściowo zrobione** |
| **6** | `InteractionComponent` za mocno powiązany z `APlayerCharacter` | 🟠 **P1** | Loose Coupling | **[x] Rozwiązane** |
| **7** | Logika pushowania fizyki siedzi w `PlayerCharacter` | 🟠 **P1** | Single Responsibility | **[ ] Do zrobienia** |
| **8** | Dużo pracy wykonywanej co klatkę w `InteractionComponent` | 🟠 **P1** | Optymalizacja / CPU | **[~] Częściowo zrobione** |
| **9** | Magic numbers w kodzie | 🟡 **P2** | Clean Code / Tuning | **[~] Częściowo zrobione** |
| **10**| Hardcoded velocity stop (`VelocityStopThreshold`) | 🟡 **P2** | Fizyka / Chaos | **[~] Częściowo zrobione** |
| **11**| Interakcja korzysta z `ECC_Visibility` zamiast dedykowanego kanału | 🟡 **P2** | Kolizje / Semantyka | **[ ] Do zrobienia** |
| **12**| Line Trace można poprawić UX-owo (Sphere Trace) | 🟡 **P2** | UX / Interakcja | **[ ] Do zrobienia** |
| **13**| `DrawDebugLine()` bezpośrednio w gameplay code | 🟡 **P2** | Profiling / Debug | **[x] Rozwiązane** |
| **14**| `LogTemp` jest używany za szeroko | 🟡 **P2** | Logging / Telemetria | **[ ] Do zrobienia** |
| **15**| Reliable RPC potrzebują rate limitu / cooldownu | 🟠 **P1** | Sieć / Anti-Spam | **[~] Częściowo zrobione** |
| **16**| `_Validate()` a Gameplay Validation (2 warstwy) | 🟠 **P1** | Architektura Sieciowa | **[x] Rozwiązane** |
| **17**| `NetworkFunctionLibrary` – pilnować, by nie stała się God Class | 🟡 **P2** | Higiena Kodu | **[x] Rozwiązane** |
| **18**| Architektura komponentowa (Component Design) | 🟢 **Zaleta** | Architektura | **[x] Utrzymywane** |
| **19**| Replikowany `StatusEffectComponent` | 🟢 **Zaleta** | Architektura | **[x] Utrzymywane** |
| **20**| Kamera jako osobny komponent (`PlayerCameraComponent`) | 🟢 **Zaleta** | Architektura | **[x] Utrzymywane** |
| **21**| Dokumentacja nie odpowiada w pełni aktualnemu kodowi | 🟠 **P1** | Dokumentacja | **[x] Rozwiązane** |
| **22**| Brak automatycznych testów multiplayer | 🔴 **P0** | QA / Produkcja | **[ ] Do zrobienia** |

---

## 🔴 P0 — Krytyczne dla Multiplayera i Bezpieczeństwa Sieci

### 1. Walidacja `Server_RequestReleaseOrThrow` i prędkości rzutu
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Problem zgłoszony:**
  Serwer bezkrytycznie przyjmował `LaunchVelocity` podane przez klienta (`return true;`), co pozwalało klientowi wymuszać dowolną prędkość rzutu w świecie gry.
* **Stan faktyczny w kodzie:**
  1. Podzielono rzut na dwa dedykowane RPC:
     - `Server_RequestForwardThrow()`: Klient wysyła **wyłącznie intencję rzutu na wprost (LPM)**. Serwer w 100% samodzielnie wylicza wektor prędkości w metodzie [`CalculateServerThrowVelocity()`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L236) (uwzględnia kąt patrzenia, impuls bazowy oraz dziedziczenie pędu biegu gracza).
     - `Server_RequestDropOrSwing(SwingVelocity)`: Klient przesyła wyliczony pęd zamachu kamery przy klawiszu `E`. Serwer waliduje NaN (`!SwingVelocity.ContainsNaN()`), a w ciele metody sztywno ucina prędkość do bezpiecznego limitu:
       ```cpp
       const float ClampedSpeed = FMath::Min(Speed, MaxSwingThrowSpeed);
       const FVector ValidatedVelocity = SwingVelocity.GetSafeNormal() * ClampedSpeed;
       ```
* **Wnioski / Co ewentualnie poprawić:** Można w przyszłości całkowicie przenieść wyliczanie `TrackedCameraSwingVelocity` na serwer na podstawie replikowanego rotatora ControlRotation, eliminując przesyłanie wektora z klienta.

---

### 2. Kompleksowa walidacja serwerowa `Server_RequestGrab`
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Problem zgłoszony:**
  `Server_RequestGrab_Validate()` sprawdzał jedynie `TargetActor != nullptr`. Brak weryfikacji odległości, widoczności (LoS), masy, zniszczenia obiektu czy przynależności komponentu.
* **Stan faktyczny w kodzie:**
  Wprowadzono 2-warstwową weryfikację. W metodzie serwerowej wywoływana jest kompleksowa funkcja [`CanGrabServer(TargetActor, ComponentToGrab)`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L265):
  1. Walidacja poprawności wskaźników (`IsValid(TargetActor)`, `IsValid(ComponentToGrab)`).
  2. Weryfikacja przynależności: `ComponentToGrab->GetOwner() == TargetActor`.
  3. Sprawdzenie stanu gracza: `CarryState == ECarryState::None || CarryState == ECarryState::RequestingGrab`.
  4. Kontrakt [`IGrabbableInterface::CanGrab(Owner)`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/IGrabbableInterface.h) (czy obiekt nie jest zajęty, czy żyje).
  5. Weryfikacja masy: `Grabbable->GetMass() <= MaxCarryMass` (35 kg).
  6. Weryfikacja odległości z uwzględnieniem promienia kolizji propa (`AllowedDist = TraceDistance + BoundsRadius + 100.0f`).
  7. Test Line of Sight (`LineTraceSingleByChannel` na kanale `ECC_Visibility` z ignorowaniem gracza i celu, celowany w `Component->Bounds.Origin`).

---

### 3. Asynchroniczny przepływ `PrimaryInteract()` (Eliminacja Desynchronizacji)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Problem zgłoszony:**
  Klient lokalnie przypisywał `GrabbedActor = HitActor` i włączał `Tick` przed wysłaniem RPC do serwera. W przypadku odrzucenia przez serwer klient pozostawał w stanie desynchronizacji.
* **Stan faktyczny w kodzie:**
  Przepływ został w pełni przestawiony na model asynchroniczny:
  ```text
  CLIENT:
    1. Wykrycie propa w celowniku (LineTrace)
    2. CarryState = ECarryState::RequestingGrab (blokada wysyłania kolejnych żądań)
    3. Server_RequestGrab(HitActor, Component)
  SERVER:
    4. Walidacja w CanGrabServer()
    5a. Jeśli SUKCES -> ExecuteGrab(...) -> Prop przechodzi w Carrying
    5b. Jeśli ODMOWA -> Client_GrabDenied() -> Klient woła ResetGrabState()
  ```
  Klient **nie mutuje już** wskaźnika `GrabbedActor` ani nie uruchamia interpolacji przed autorytatywną zgodą serwera.

---

## 🟠 P1 — Architektura Interakcji, Stanu i Kinetyki

### 4. Formalna maszyna stanów niesienia (`ECarryState`)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h#L11-L24)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny w kodzie:**
  Wprowadzono dedykowany typ wyliczeniowy `ECarryState`:
  - `ECarryState::None` – brak interakcji, ręce wolne.
  - `ECarryState::RequestingGrab` – klient wysłał żądanie i oczekuje na decyzję serwera.
  - `ECarryState::Carrying` – obiekt jest aktywnie niesiony i prowadzony sweepem.
  - `ECarryState::Releasing` – faza upuszczania lub wyrzutu (zapobiega ponownemu chwytaniu w tej samej klatce).

---

### 5. Reprezentacja Transformu Niesionego Obiektu (Server vs Client)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L198)
* **Status:** `[~] Częściowo zrobione`
* **Problem zgłoszony:**
  `UpdateCarriedPropTransform()` wykonuje `SetActorLocationAndRotation` z interpolacją lokalną w `TickComponent()`.
* **Stan faktyczny w kodzie:**
  - Prowadzenie propa odbywa się przez **Kinematic Sweep Follow** (wzorzec tarczy ochronnej). Prop jest odpięty ze sztywnej hierarchii (`DetachFromActor(KeepWorld)`), a jego transform jest aktualizowany sweepem z testem kolizji `bSweep = true`.
  - Serwer autorytatywnie egzekwuje kolizje i pchanie obiektów (`HandleSweepCollision`).
* **Do zrobienia:**
  Rozdzielić prezentację wizualną: na kliencie zdalnym (Remote Client) wygładzać pozycję niesionego propa poprzez standardową replikację i interpolację `ReplicatedMovement`, upewniając się, że `TickComponent` interpoluje transform wyłącznie u kontrolującego właściciela (Autonomous Proxy) oraz na serwerze (Authority).

---

### 6. Rozłączenie (Decoupling) `InteractionComponent` od `APlayerCharacter`
* **Pliki:**
  - [`Source/MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h)
  - [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L79-L86)
* **Status:** `[x] Rozwiązane`
* **Problem zgłoszony:**
  Bezpośrednie rzutowanie `Cast<APlayerCharacter>(PawnOwner)` łamało zasady warstwy `Shared`.
* **Stan faktyczny w kodzie:**
  Stworzono interfejs [`ICarryAnchorProviderInterface`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h):
  - `GetHoldAnchorComponent()`
  - `GetCarryEyeHeightOffset()`
  - `GetMaxPushableMass()`
  - `GetPlayerPushForce()`
  `UInteractionComponent` nie zawiera ani jednej linijki odwołującej się do `APlayerCharacter`. Może być przypięty do dowolnego humanoida AI, potwora czy alternatywnej klasy postaci.

---

### 7. Wydzielenie logiki pchania fizyki z `PlayerCharacter`
* **Plik:** [`Source/MyProject/Player/PlayerCharacter.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Player/PlayerCharacter.cpp#L125)
* **Status:** `[ ] Do zrobienia`
* **Problem zgłoszony:**
  W `APlayerCharacter::MoveBlockedBy()` znajduje się bezpośrednia logika fizycznego pchania (`IsSimulatingPhysics`, `GetMass`, `AddForceAtLocation`), co obciąża klasę postaci.
* **Proponowane rozwiązanie:**
  Wyekstrahować logikę do dedykowanego komponentu, np. `UPhysicsPushComponent` lub przenieść do biblioteki [`UKineticForceLibrary`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h), tak by `APlayerCharacter::MoveBlockedBy` delegował wykonanie jednym wywołaniem:
  ```cpp
  PushComponent->HandleMoveBlocked(Impact);
  ```

---

### 8. Optymalizacja operacji wykonywanych w `TickComponent`
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L162)
* **Status:** `[~] Częściowo zrobione`
* **Problem zgłoszony:**
  `TickComponent` wykonuje co klatkę m.in. `PropPrim->GetOverlappingComponents(Overlaps)` w metodzie `SuppressOverlappingHeavyPhysics`.
* **Stan faktyczny:**
  - `TickComponent` posiada `bStartWithTickEnabled = false` i jest wygaszony (`SetComponentTickEnabled(false)`), gdy gracz nie niesie żadnego przedmiotu (Zero-Tick Idle).
  - W trakcie niesienia wykonywane jest jednak `GetOverlappingComponents()`.
* **Do zrobienia:**
  Zastąpić sprawdzanie `GetOverlappingComponents()` zdarzeniami `OnComponentBeginOverlap` / `OnComponentEndOverlap` podpinanymi dynamicznie na niesionym propie na czas chwytu, utrzymując lekką tablicę `TArray<TWeakObjectPtr<UPrimitiveComponent>> OverlappingHeavyProps`.

---

## 🟡 P2 — Tuning, Fizyka, Kolizje i Konwencje

### 9. Eliminacja Magic Numbers
* **Pliki:** [`InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp), [`PlayerCharacter.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Player/PlayerCharacter.cpp)
* **Status:** `[~] Częściowo zrobione`
* **Stan faktyczny:**
  Większość kluczowych wartości (`TraceDistance`, `MaxCarryMass`, `ThrowImpulseStrength`, `CarryBreakDistance`, `MaxSwingThrowSpeed`, `VelocityStopThreshold`, `MaxPushableMass`, `PlayerPushForce`) została wyciągnięta do pól `UPROPERTY(EditDefaultsOnly)`.
* **Do zrobienia:**
  W kodzie pozostały drobne stałe geometryczne (np. `constexpr float HoldRadius = 110.0f;`, `PitchRad`, offset Z). Warto przenieść je do struktury konfiguracyjnej `FCarrySocketConfig`.

---

### 10. Bezpieczeństwo tłumienia prędkości (`VelocityStopThreshold`)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L64)
* **Status:** `[~] Częściowo zrobione`
* **Stan faktyczny:**
  Wartość `60.0f` została wyciągnięta do konfigurowalnego pola `VelocityStopThreshold`. Służy wyłącznie jako zabezpieczenie przed drżeniem obiektów o masie $> 100\text{ kg}$ w kontakcie z graczem.
* **Rekomendacja:**
  Utrzymać jako ostateczny bezpiecznik (safety net), jednocześnie dbając o odpowiedni `LinearDamping` i `AngularDamping` w materiałach fizycznych `PhysicalMaterial`.

---

### 11. Dedykowany kanał kolizji interakcji (`ECC_Interaction`)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L396)
* **Status:** `[ ] Do zrobienia`
* **Problem:**
  Interakcja korzysta z `ECC_Visibility`. Może to powodować problemy, gdy przezroczyste szyby blokują interakcję z przedmiotem za nimi lub odwrotnie.
* **Do zrobienia:**
  Skonfigurować w `DefaultEngine.ini` własny kanał kolizji (np. `ECC_GameTraceChannel1` jako `ECC_Interaction`) i przestawić `PerformTrace`.

---

### 12. Ulepszenie UX detekcji interakcji (Sphere Trace zamiast Line Trace)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L390)
* **Status:** `[ ] Do zrobienia`
* **Problem:**
  Czysty `LineTraceSingleByChannel` utrudnia wycelowanie w małe obiekty (np. monety, klucze, małe flakoniki).
* **Do zrobienia:**
  Zastąpić pojedynczy promień testem `SweepSingleByChannel` ze sferą o promieniu np. $10\text{–}15\text{ cm}$.

---

### 13. Zabezpieczenie debugowania rysunkowego (`DrawDebugLine`)
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L411)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Wszelkie wywołania `DrawDebugLine` zostały otoczone dyrektywą preprocesora:
  ```cpp
  #if ENABLE_DRAW_DEBUG
      DrawDebugLine(GetWorld(), CameraLocation, OutHit.ImpactPoint, FColor::Green, false, 2.0f, 0, 2.0f);
  #endif
  ```
  W buildach produkcyjnych (Shipping) kod ten jest całkowicie usuwany przez kompilator.

---

### 14. Dedykowane kategorie logowania zamiast `LogTemp`
* **Pliki:** Cały projekt `Source/MyProject/`
* **Status:** `[ ] Do zrobienia`
* **Problem:**
  Nadużywanie ogólnego `LogTemp` utrudnia filtrowanie komunikatów na serwerze i kliencie.
* **Do zrobienia:**
  Zdefiniować w nagłówku dedykowane kategorie logowania:
  - `DECLARE_LOG_CATEGORY_EXTERN(LogDungeonInteraction, Log, All);`
  - `DECLARE_LOG_CATEGORY_EXTERN(LogDungeonPhysics, Log, All);`
  - `DECLARE_LOG_CATEGORY_EXTERN(LogDungeonNetwork, Log, All);`
  - `DECLARE_LOG_CATEGORY_EXTERN(LogDungeonElements, Log, All);`

---

### 15. Rate Limiting i ochrona przed spamem RPC
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L326)
* **Status:** `[~] Częściowo zrobione`
* **Stan faktyczny:**
  - W `PrimaryInteract()` klient sprawdza:
    ```cpp
    if (CarryState != ECarryState::None) return;
    ```
    co uniemożliwia wielokrotne wysłanie `Server_RequestGrab` przed otrzymaniem odpowiedzi z serwera.
* **Do zrobienia:**
  Dodać po stronie serwera znacznik `double LastInteractionServerTime` i odrzucać żądania przychodzące częściej niż co np. $0.15\text{ s}$ na wypadek zmodyfikowanego klienta sieciowego.

---

### 16. Rozdział Walidacji: RPC Validation vs Gameplay Validation
* **Plik:** [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.cpp#L446)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Zgodnie z najlepszymi praktykami Unreal Engine:
  - `_Validate()` weryfikuje jedynie poprawność strukturalną pakietu sieciowego (brak `nullptr`, brak `NaN`).
  - Logika biznesowa (zasięg, masa, LoS, cooldown) została oddelegowana do metody domenowej `CanGrabServer()`. Błąd biznesowy nie rozłącza gracza błędem sieciowym, lecz zwraca elegancką odmowę `Client_GrabDenied()`.

---

### 17. Higiena `NetworkFunctionLibrary` (Zapobieganie God Class)
* **Plik:** [`Source/MyProject/Networking/NetworkFunctionLibrary.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Networking/NetworkFunctionLibrary.h)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Biblioteka zawiera wyłącznie czyste utility sieciowe (`REQUIRE_AUTHORITY`, `HasAuthority`, `GetNetRolePrefix`, konfigurację kwantyzacji). Nie zawiera ani jednej metody związanej z logiką gry czy walidacją poszczególnych mechanik.

---

## 🟢 Zidentyfikowane Mocne Strony Architektury

### 18. Modułowa Architektura Komponentowa (Component-Driven Design)
- Wyraźny podział odpowiedzialności: brak monolitycznych klas typu "God Actor".
- Logika rozdzielona na wyspecjalizowane serwisy: [`DamageableComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/DamageableComponent/DamageableComponent.h), [`StatusEffectComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h), [`InteractionComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h), [`KnockbackComponent`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Kinetic/Components/KnockbackComponent/KnockbackComponent.h).

### 19. Wydajny Silnik Statusów i Reakcji Chemii (`StatusEffectComponent`)
- Replikowana tablica instancji o wielkości zaledwie 9 bajtów per status.
- Wzorzec *Zero-Bandwidth Timers* (`ServerEndTime`) eliminujący obciążenie łącza.
- Rejestr danych `FStatusEffectRegistry` i dwufazowa ewaluacja (`ElementalPriorityPipeline`).

### 20. Izolacja Kamery (`PlayerCameraComponent`)
- Sterowanie zoomem, wygładzanie i perspektywa są całkowicie odseparowane od logiki postaci i poruszania się.

---

## 🟠 21. Synchronizacja Dokumentacji z Kodem Źródłowym

* **Status:** `[x] Rozwiązane`
* **Stan po aktualizacji:**
  Zaktualizowano pliki [ARCHITECTURE.md](file:///E:/UE_PROJECTS/MyProject/.context/ARCHITECTURE.md) oraz [MEMORY.md](file:///E:/UE_PROJECTS/MyProject/.context/MEMORY.md).
  Wyeliminowano wszelkie nieaktualne wzmianki o `ACombatCharacterBase` oraz o "braku networkingu". Dokumentacja wiernie odzwierciedla obecny stan `main`: kinematyczny model oparty o stabilny `CharacterMovementComponent` (CMC), autorytatywną sieć Co-op, mechanizmy pułapek (`APistonTrap`, `APressurePlateProp`) oraz specyfikację [THEME_PARK_SPECIFICATION.md](file:///E:/UE_PROJECTS/MyProject/.context/THEME_PARK_SPECIFICATION.md).

---

## 🔴 22. Automatyczne Testy Multiplayer (Automation Testing)

* **Status:** `[ ] Do zrobienia`
* **Problem:**
  Brak zestawu testów jednostkowych i funkcjonalnych silnika (`FAutomationTestBase`).
* **Wymagany zakres testów:**
  1. **Testy Interakcji i Chwytania:**
     - Gracz próbuje chwycić obiekt z odległości $> 300\text{ cm}$ (oczekiwane: odmowa).
     - Gracz próbuje chwycić obiekt zza ściany (oczekiwane: blokada LoS).
     - Gracz próbuje chwycić obiekt o masie $> 35\text{ kg}$ (oczekiwane: odmowa).
  2. **Testy Rywalizacji w Co-op (Race Conditions):**
     - Gracz A i Gracz B próbują chwycić ten sam obiekt w tej samej klatce.
     - Gracz A rozłącza się w trakcie niesienia obiektu (oczekiwane: bezpieczne upuszczenie).
     - Obiekt zostaje zniszczony w trakcie niesienia (oczekiwane: reset stanu gracza bez awarii silnika).
  3. **Testy Integralności Fizyki:**
     - Stabilność 4 graczy i kilkunastu ciał sztywnych przy opóźnieniach sieciowych (Ping $100\text{–}150\text{ ms}$, packet loss $2\%$).

---

## 🚀 Plan Wdrożenia i Kolejność Prac (Action Plan)

```mermaid
graph TD
    subgraph "Sprint 1: Dokończenie Bezpieczeństwa Sieciowego (P0/P1)"
        S1_1["Wprowadzenie server cooldown na RPC interakcji (pkt 15)"]
        S1_2["Event-driven overlap dla SuppressOverlappingHeavyPhysics (pkt 8)"]
    end

    subgraph "Sprint 2: Refaktoryzacja Fizyki Postaci (P1)"
        S2_1["Wydzielenie pchania fizyki z PlayerCharacter do komponentu (pkt 7)"]
        S2_2["Wprowadzenie kanału ECC_Interaction i Sphere Trace (pkt 11, 12)"]
    end

    subgraph "Sprint 3: Clean Code & Telemetria (P2)"
        S3_1["Własne kategorie logowania LogDungeon* (pkt 14)"]
        S3_2["Struktura FCarrySocketConfig dla stałych matematycznych (pkt 9)"]
    end

    subgraph "Sprint 4: Theme Park Loch v0.1 & Testy (P0/P1)"
        S4_1["Implementacja C++ AProjectileLauncherTrap"]
        S4_2["Blueprinty: BP_PlankShield_Wood i BP_GlassOrb_*"]
        S4_3["Stworzenie testów automatycznych Automation Tests (pkt 22)"]
    end

    Sprint 1 --> Sprint 2
    Sprint 2 --> Sprint 3
    Sprint 3 --> Sprint 4
```

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
| **5** | Carry transform jest wykonywany lokalnie | 🟠 **P1** | Multiplayer / Prezentacja | **[x] Rozwiązane** |
| **6** | `InteractionComponent` za mocno powiązany z `APlayerCharacter` | 🟠 **P1** | Loose Coupling | **[x] Rozwiązane** |
| **7** | Logika pushowania fizyki siedzi w `PlayerCharacter` | 🟠 **P1** | Single Responsibility | **[x] Rozwiązane** |
| **8** | Dużo pracy wykonywanej co klatkę w `InteractionComponent` | 🟠 **P1** | Optymalizacja / CPU | **[x] Rozwiązane** |
| **9** | Magic numbers w kodzie (`FCarrySocketConfig`) | 🟡 **P2** | Clean Code / Tuning | **[x] Rozwiązane** |
| **10**| Hardcoded velocity stop (`VelocityStopThreshold`) | 🟡 **P2** | Fizyka / Chaos | **[x] Rozwiązane** |
| **11**| Interakcja korzysta z `ECC_Visibility` zamiast dedykowanego kanału | 🟡 **P2** | Kolizje / Semantyka | **[x] Rozwiązane** |
| **12**| Line Trace można poprawić UX-owo (Sphere Trace) | 🟡 **P2** | UX / Interakcja | **[x] Rozwiązane** |
| **13**| `DrawDebugLine()` bezpośrednio w gameplay code | 🟡 **P2** | Profiling / Debug | **[x] Rozwiązane** |
| **14**| `LogTemp` jest używany za szeroko (Dedykowane kategorie) | 🟡 **P2** | Logging / Telemetria | **[x] Rozwiązane** |
| **15**| Reliable RPC potrzebują rate limitu / cooldownu | 🟠 **P1** | Sieć / Anti-Spam | **[x] Rozwiązane** |
| **16**| `_Validate()` a Gameplay Validation (2 warstwy) | 🟠 **P1** | Architektura Sieciowa | **[x] Rozwiązane** |
| **17**| `NetworkFunctionLibrary` – pilnować, by nie stała się God Class | 🟡 **P2** | Higiena Kodu | **[x] Rozwiązane** |
| **18**| Architektura komponentowa (Wydzielenie `UPhysicsCarryComponent`) | 🟢 **Zaleta** | Architektura | **[x] Utrzymywane i Rozbudowane** |
| **19**| Replikowany `StatusEffectComponent` | 🟢 **Zaleta** | Architektura | **[x] Utrzymywane** |
| **20**| Kamera jako osobny komponent (`PlayerCameraComponent`) | 🟢 **Zaleta** | Architektura | **[x] Utrzymywane** |
| **21**| Dokumentacja nie odpowiada w pełni aktualnemu kodowi | 🟠 **P1** | Dokumentacja | **[x] Rozwiązane** |
| **22**| Brak automatycznych testów multiplayer | 🔴 **P0** | QA / Produkcja | **[ ] Do zrobienia** |

---

## 🔴 P0 — Krytyczne dla Multiplayera i Bezpieczeństwa Sieci

### 1. Walidacja `Server_RequestReleaseOrThrow` i prędkości rzutu
* **Plik:** [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny w kodzie:**
  1. Podzielono rzut na dwa dedykowane RPC:
     - `Server_RequestForwardThrow()`: Klient wysyła **wyłącznie intencję rzutu na wprost (R / LPM)**. Serwer w 100% samodzielnie wylicza wektor prędkości w metodzie `CalculateServerThrowVelocity()` (uwzględnia kąt patrzenia, impuls bazowy oraz dziedziczenie pędu biegu gracza).
     - `Server_RequestDropOrSwing(SwingVelocity)`: Klient przesyła wyliczony pęd zamachu kamery przy klawiszu `E`. Serwer waliduje NaN (`!SwingVelocity.ContainsNaN()`), a w ciele metody sztywno ucina prędkość do bezpiecznego limitu (`MaxSwingThrowSpeed`).

---

### 2. Kompleksowa walidacja serwerowa `Server_RequestGrab`
* **Plik:** [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny w kodzie:**
  1. Dwupoziomowa walidacja RPC (strukturalna w `_Validate`, domenowa w `_Implementation` z `CanGrabServer`).
  2. Weryfikacja odległości z uwzględnieniem promienia propa, weryfikacja Line of Sight, masy (`MaxCarryMass`) oraz stanu gotowości obiektu (`IGrabbable::CanGrab`).

---

### 3. Asynchroniczna autorytatywność `TryGrab` (Zero lokalnego desyncu)
* **Plik:** [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny w kodzie:**
  1. Klient po wykryciu obiektu ustawia stan przejściowy `ECarryState::RequestingGrab` i wysyła `Server_RequestGrab`.
  2. Dopiero gdy serwer zatwierdzi chwyt i nada autorytet, obiekt przechodzi w stan trzymania (`ECarryState::Carrying`).
  3. W przypadku odmowy serwera wywoływany jest `Client_GrabDenied()`, który czyści stan bez artefaktów wizualnych.

---

### 4. Maszyna Stanów Niesienia (`ECarryState`)
* **Plik:** [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Stan maszyny stanów: `None`, `RequestingGrab`, `Carrying`, `Releasing`.

---

### 5. Kinematyczny Sweep i Replikacja Niesienia
* **Plik:** [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.cpp)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Kinematyczny sweep ze śledzeniem kolizji prowadzony jest wyłącznie na Serwerze oraz u gracza lokalnego (`IsLocallyControlled`). Pozostali gracze odbierają ruch wygładzony przez replikację ruchu aktora (`ReplicatedMovement`).

---

### 6 & 7. Loose Coupling i Single Responsibility (Wydzielenie Komponentów)
* **Pliki:**
  - [`Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/InteractionComponent/InteractionComponent.h)
  - [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h)
  - [`Source/MyProject/Player/PlayerCharacter.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Player/PlayerCharacter.cpp)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  - `UInteractionComponent`: Odchudzony do czystego wykrywania (Sphere/Line Trace), obsługi `IInteractable` (dźwignie, przyciski) oraz wsparcia dla akcji natychmiastowych i przytrzymania (Hold/Channeling 5s z delegatem postępu dla UI).
  - `UPhysicsCarryComponent`: Samodzielny podsystem fizyczny do noszenia, rzucania (Klawisz R / LPM), zamachu myszką (Klawisz E) i tłumienia jitteru Chaos.

---

### 8. Optymalizacja operacji wykonywanych w `TickComponent`
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Zastosowano Event-Driven Overlaps zamiast kosztownych zapytań `GetOverlappingComponents` co klatkę.

---

### 9. Eliminacja Magic Numbers (`FCarrySocketConfig`)
* **Pliki:** [`Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny po wdrożeniu:**
  Wprowadzono konfigurowalną strukturę `FCarrySocketConfig` zawierającą:
  - `HoldDistance = 110.0f`
  - `EyeHeightOffsetZ = -15.0f`
  - `MinPitch = -50.0f` oraz `MaxPitch = 50.0f`
  - `AnchorInterpSpeed = 20.0f`
  - `PropInterpSpeed = 25.0f`
  - `SwingInterpSpeed = 16.0f`

---

### 10. Bezpieczeństwo tłumienia prędkości (`VelocityStopThreshold`)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Wyciągnięte do konfigurowalnego pola `VelocityStopThreshold = 60.0f`, zintegrowane w `UKineticForceLibrary::SuppressHeavyPhysicsJitter()`. Służy jako bezpiecznik programowy przed mikro-drżeniem ciężkich brył Chaos.

---

### 11 & 12. Dedykowany kanał kolizji i Sphere Trace UX
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Zarejestrowano kanał `Interaction` w `DefaultEngine.ini`, dodano wsparcie dla Sphere Trace ze sferą $12\text{ cm}$.

---

### 13. Zabezpieczenie debugowania rysunkowego (`#if ENABLE_DRAW_DEBUG`)
* **Status:** `[x] Rozwiązane`

---

### 14. Dedykowane kategorie logowania (`LogDungeon*`)
* **Pliki:** [`Source/MyProject/Logging/DungeonLogCategories.h`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Logging/DungeonLogCategories.h) & [`.cpp`](file:///E:/UE_PROJECTS/MyProject/Source/MyProject/Logging/DungeonLogCategories.cpp)
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny po wdrożeniu:**
  Utworzono i podpięto kategorie:
  - `LogDungeonInteraction`
  - `LogDungeonPhysics`
  - `LogDungeonNetwork`
  - `LogDungeonMechanisms`
  - `LogDungeonElements`
  Wszystkie wywołania `LogTemp` w projekcie zostały wyeliminowane.

---

### 15 & 16. Rate Limiting i 2-Warstwowa Walidacja RPC
* **Status:** `[x] Rozwiązane`
* **Stan faktyczny:**
  Wprowadzono `MinInteractionInterval = 0.15f` zarówno po stronie klienta (debounce), jak i serwera (rate limit).

---

### 17, 18, 19, 20, 21. Higiena Architektury i Dokumentacja
* **Status:** `[x] Rozwiązane / Utrzymywane`

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
     - Stabilność 4 graczy i kilkunastu ciał sztywnych przy opóźnieniach sieciowych (Ping $100–150\text{ ms}$, packet loss $2\%$).

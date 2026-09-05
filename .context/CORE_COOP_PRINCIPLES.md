# Architektura Sieciowa i Zasady Implementacji Co-op (1–6 Graczy)

## 1. Misja i Cel Dokumentu
Dokument stanowi **kontrakt architektoniczny (Network Architecture RFC)** dla gry *Dungeon Crawler*.
Definiuje żelazne reguły implementacji sieciowej w Unreal Engine 5.8 C++ (`MYPROJECT_API`) dla trybu kooperacyjnego i potyczek w skali **1–6 graczy**, z naciskiem na **maksymalną wydajność procesora serwera, minimalne zużycie pasma (Zero-Bandwidth mindset) oraz stabilność fizyki Chaos**.

Każdy nowy system, komponent i aktor wprowadzany do projektu musi spełniać poniższe standardy przed włączeniem do głównej gałęzi kodu.

---

## 2. Podstawowy Model Sieciowy (Server-Authoritative First)

* **Serwer jako Jedyne Źródło Prawdy (Single Source of Truth):**
  * Tylko serwer (`REQUIRE_AUTHORITY()` / `NetUtils::HasAuthority(this) == true`) ma prawo:
    * Zmieniać punkty życia/durability (`UDamageableComponent`).
    * Nakładać, przedłużać, usuwać i przetwarzać reakcje statusów żywiołowych (`UStatusEffectComponent`).
    * Niszczyć obiekty, detonować ładunki (`AVolatileProp`) i aplikować siły radialne (`UKineticForceLibrary`).
    * Rozstrzygać podnoszenie, rzucanie i upuszczanie propów (`UInteractionComponent`).
* **Scentralizowana warstwa sieciowa (`Source/MyProject/Networking`):**
  * Do zabezpieczania wejścia do metod autorytatywnych stosujemy deklaratywne makro strażnicze:
    ```cpp
    REQUIRE_AUTHORITY(); // Wychodzi z metody jeśli obiekt nie ma autorytetu
    REQUIRE_AUTHORITY_RET(ReturnValue); // Wychodzi ze zwróceniem wartości
    ```
  * Do sprawdzania logicznego: `NetUtils::HasAuthority(this)` (odporne na `nullptr`, wspiera aktory i komponenty).
  * Do formatowania logów stosujemy `NetUtils::GetNetRolePrefix(this)` (automatyczny prefiks `[Server]`, `[Client]`, `[Standalone]`).
* **Rola Klienta (Dumb Terminal + Client-Side Prediction):**
  * Klient przetwarza lokalny input (ruch CMC, sterowanie kamerą) i natychmiast wysyła intencje do serwera.
  * Klient **nigdy bezpośrednio nie mutuje stanu gry** ani innych aktorów.
  * Zmiany stanu odbiera reaktywnie poprzez funkcje `OnRep_...` lub zdarzenia delegatów.

---

## 3. Złote Zasady Wydajności i Optymalizacji Sieciowej (High Performance Netcode)

### Zasada 1: Wzorzec "Zero-Bandwidth Timers" (Optymalizacja Czasu i Statusów)
* **Antywzorzec:** Replikowanie malejącej zmiennej `RemainingDuration` w każdej klatce (60–120 razy na sekundę). Prowadzi to do natychmiastowego zapchania pasma wysyłania (bandwidth choke).
* **Reguła Projektu:**
  * Serwer replikuje wyłącznie **znacznik czasu zakończenia** w oparciu o czas świata:
    $$\text{ServerEndTime} = \text{GetWorld()->GetTimeSeconds()} + \text{Duration}$$
  * Pakiet wysyłany jest **tylko raz** (przy nałożeniu lub odświeżeniu statusu).
  * Podczas trwania całego efektu zużycie pasma wynosi **dokładnie 0 bajtów/s**.
  * Klient i UI lokalnie obliczają pozostały czas:
    $$\text{LocalRemaining} = \text{FMath::Max}(0.0f, \text{ServerEndTime} - \text{GetWorld()->GetTimeSeconds()})$$

### Zasada 2: Zgodność Kontenerów Danych z UHT (Zakaz replikacji `TMap`)
* **Ograniczenie silnika:** Unreal Header Tool (UHT) **nie obsługuje** replikacji map asocjacyjnych (`UPROPERTY(Replicated) TMap<K, V>`).
* **Reguła Projektu:**
  * Wszelkie replikowane kolekcje stanów muszą być zaimplementowane jako:
    1. Zwarte tablice struktur (`TArray<FReplicatedItem>`) z dedykowanym `OnRep`, lub
    2. Struktury `FFastArraySerializer` (dla zaawansowanych, często zmieniających się list ze wsparciem delta-serializacji).

### Zasada 3: Wzorzec Kinematycznego Prowadzenia ze Sweepem (Kinematic Sweep & Anti-Bulldozer Shield Pattern)
* **Problem Sztywnego `AttachToComponent`:** Tradycyjny `AttachToComponent(SnapToTarget)` tworzy sztywną hierarchię transformów. Gdy gracz idzie w ścianę lub beczkę 200 kg, podpięty obiekt staje się niewzruszonym "spychaczem" (bulldozer) o nieskończonej masie, przepychającym lub miażdżącym przeszkody w solverze Chaos. Z kolei `PhysicsHandle` drży i nie replikuje się w sieci.
* **Rozwiązanie Projektu (Kinematic Follow with Sweep):**
  * **Odpięcie w przestrzeni świata:** Obiekt nie jest podpinany hierarchicznie do postaci (`DetachFromActor(KeepWorld)`).
  * **Kinematyczny Sweep w On-Demand Tick:** Pozycja trzymanego obiektu jest płynnie interpolowana (`VInterpTo`/`RInterpTo`) w stronę punktu kotwicy rąk (`HoldAnchorComponent`) z włączonym testem kolizji (`bSweep = true`).
  * **Funkcja Tarczy (Blocking Shield):** Trzymany prop ma wyłączone ignorowanie innych obiektów, zachowując pełną blokadę pocisków (`ECC_WorldDynamic`), magii i uderzeń.
  * **Inteligentna kontrola masy (Anti-Bulldozer):**
    * Jeśli prop dotknie obiektu $\le \text{MaxPushableMass}$ (100 kg, np. skrzynia 60 kg) $\rightarrow$ aplikowana jest rzeczywista siła pchania gracza (`AddForceAtLocation`), pozwalająca na przesuwanie przeszkód tarczą.
    * Jeśli prop dotknie lub nałoży się na obiekt $> \text{MaxPushableMass}$ (np. głaz 200 kg) $\rightarrow$ solver Chaosu jest tłumiony (`SetPhysicsLinearVelocity(ZeroVector)`), całkowicie eliminując efekt spychacza i szturnięcia od boku przy obrocie kamery.
  * **Zerwanie chwytu (Break Distance):** Jeśli gracz napiera na nieustępliwą ścianę i odległość rąk od zablokowanego propa przekroczy próg (`CarryBreakDistance = 70.0f`), chwyt natychmiast pęka, a obiekt upada pod stopy.
  * **Fizyka Upuszczenia i Rzutu Zamachem:**
    * Przy zwykłym marszu naciśnięcie `E` upuszcza obiekt pod nogi z prędkością zero (grawitacja ściąga obiekt na dół).
    * Przy dynamicznym obrocie kamerą (zamach myszką) wyliczana jest prędkość kątowa zamachu na promieniu trzymania, nadając propowi pęd rzutu po łuku.
    * Dedykowany rzut w przód (LPM) nadaje pełny impuls wystrzału na wprost.

### Zasada 4: Separacja Logiki Domenowej od Kosmetyki (Logic vs FX)
* **Logika Biznesowa (Tylko Serwer / OnRep):**
  * Obliczenia geometryczne, sprawdzanie odporności, kalkulacja obrażeń, modyfikacje statystyk.
* **Prezentacja i FX (Klienci):**
  * Odtwarzanie dźwięków (Cue), spawn emiterów cząsteczek (Niagara), drgania kamery (Camera Shake), etykiety debugowe.
  * Efekty jednorazowe wywoływane są za pośrednictwem:
    * Zdarzeń `OnRep` (preferowane, deterministyczne, odporne na dołączanie graczy w trakcie gry – *Join-in-Progress*).
    * Zdalnych wywołań `UFUNCTION(NetMulticast, Unreliable)` (wyłącznie dla ulotnych efektów wizualnych/dźwiękowych).

### Zasada 5: Walidacja Intencji Klienta (Server RPC with Validation)
* Każde żądanie akcji wysyłane z klienta (`Server_Request...`) musi posiadać implementację `_Validate`:
  * Weryfikacja odległości interakcji (czy gracz nie próbuje podnieść przedmiotu z odległości 20 metrów).
  * Weryfikacja stanu gracza (czy gracz nie jest ogłuszony, powalony lub martwy).
  * Zabezpieczenie przed manipulacją pakietami i błędami skrajnymi (np. clamp maksymalnej prędkości rzutu `MaxSwingThrowSpeed = 2200.0f`).

### Zasada 6: Kompresja Różnicowa i Kwantyzacja (Delta Compression & Quantization)
* **Problem:** Wysyłanie pełnych wektorów transformu (`FVector` = 12/24 bajty) w każdej klatce zużywa ogromne pasmo.
* **Natywna obsługa w UE:** Silnik posiada wbudowaną kompresję różnicową, ale **wymaga odpowiedniej konfiguracji w C++**:
  * **Kwantyzacja `FRepMovement` dla propów (`AInteractivePropBase`):**
    ```cpp
    // Ograniczenie precyzji do 2 miejsc po przecinku (zamiast pełnego float/double)
    ReplicatedMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
    ReplicatedMovement.VelocityQuantizationLevel = EVectorQuantization::RoundWholeNumber;
    ReplicatedMovement.RotationQuantizationLevel = ERotatorQuantization::ByteComponents; // 1 bajt per oś (0-255) zamiast 4 bajtów!
    ```
  * **Uśpienie sieciowe (Net Dormancy):** Obiekt, który przestał się poruszać i spoczywa na ziemi, powinien przejść w stan uśpienia (`NetDormancy = DORM_DormantAll`), co redukuje wysyłanie pakietów do zera.
  * **Postacie Graczy:** `CharacterMovementComponent` natywnie wysyła delty (`FSavedMove`) i serwerowe korekty błędów, minimalizując zużycie łącza.

---

## 4. Specyfikacja Techniczna Kluczowych Modułów

### 1. `APlayerCharacter` & Kontrola Ruchu
* Model oparty na `ACharacter` i `CharacterMovementComponent` (CMC).
* **Ruch:** Wykorzystuje natywną predykcję klienta i korektę serwera.
* **Fizyczne Pchanie Ciałem:** Realizowane przez przeciążenie `MoveBlockedBy` z limitem masy $100\text{ kg}$ i siłą $150\,000\text{ N}$.
* **Odrzuty (Knockback):** Wykonywane przez `Character->LaunchCharacter()`. Serwer wywołuje funkcję, a CMC replikuje zmianę prędkości i trajektorię na klientów w ramach protokołu ruchu postaci.

### 2. `UDamageableComponent`
* `bReplicates = true`, zarejestrowany przez `SetIsReplicatedByDefault(true)`.\
* `CurrentDurability` oznaczone jako `UPROPERTY(ReplicatedUsing = OnRep_CurrentDurability)`.
* `ApplyDamage` oraz `ApplyKineticImpact` posiadają deklaratywnego strażnika:
  ```cpp
  REQUIRE_AUTHORITY();
  ```
* Funkcja `OnRep_CurrentDurability` rozgłasza lokalne delegaty `OnHealthChanged` / `OnDurabilityChanged`, zasilając pasek zdrowia UI u każdego klienta.

### 3. `UStatusEffectComponent` & `UElementalChemistryLibrary`
* `bReplicates = true`.
* **Hierarchia Ewaluacji Chemii Żywiołów (Elemental Priority Pipeline):**
  1. **Faza 1 (Żywioł vs Powłoka):** Sprawdzenie czy przychodzący żywioł reaguje z aktywnym statusem na powierzchni (np. Ogień uderza w Mokry cel $\rightarrow$ `Steam_Extinguish`). Jeśli reakcja neutralizuje żywioł (`bConsumeIncomingStatus = true`), następuje odparowanie/ugaszanie i wczesne zakończenie.
  2. **Faza 2 (Żywioł vs Materiał pod spodem):** Jeśli żywioł nie został skonsumowany przez powłokę, sprawdzana jest odporność materiałowa (`CanMaterialReceiveStatus`). Kamień i metal odrzucają ogień, chroniąc obiekt przed niepoprawnym zapłonem.
* Tablica aktywnych statusów:
  ```cpp
  UPROPERTY(ReplicatedUsing = OnRep_ActiveEffects)
  TArray<FActiveStatusEffectInstance> ReplicatedEffects;
  ```
* Cykl życia, DoT oraz ewaluacja chemii wykonywane są **wyłącznie na serwerze**.

### 4. `AInteractivePropBase` & `AVolatileProp`
* `bReplicates = true`, `SetReplicateMovement(true)`.
* Skonfigurowana kwantyzacja: `ReplicatedMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals`.
* Obiekty fizyczne w stanie spoczynku przechodzą w tryb uśpienia (*Sleep Rigid Body*), całkowicie zwalniając wątek fizyki i serwer z replikacji transformu.
* Wybuchy w `AVolatileProp`:
  * Kalkulacja radialna i aplikacja `UKineticForceLibrary::ApplyExplosion` odbywa się na serwerze.
  * Efekty wizualne wybuchu (ogień, dym, dźwięk) wysyłane są do klientów za pomocą lekkiego `Multicast_PlayExplosionEffects(FVector Location)`.

### 5. `UInteractionComponent`
* Podnoszenie i rzucanie propów zorganizowane w schemacie:
  ```cpp
  // 1. Klient wciska klawisz 'E' (Podnieś / Upuść / Rzuć zamachem)
  void PrimaryInteract();

  // 2. Klient wciska LPM (Dedykowany silny rzut w przód)
  void ThrowCurrentProp();

  // 3. RPC do serwera o podniesienie konkretnego aktora
  UFUNCTION(Server, Reliable, WithValidation)
  void Server_RequestGrab(AActor* TargetProp, UPrimitiveComponent* ComponentToGrab);

  // 4. RPC do serwera o upuszczenie lub rzut z wyliczonym wektorem pędu
  UFUNCTION(Server, Reliable, WithValidation)
  void Server_RequestReleaseOrThrow(bool bIsThrow, const FVector_NetQuantize& LaunchVelocity);
  ```

---

## 5. Metodologia Wdrażania i Testowania (Atomic Step-by-Step Protocol)

### Zasada Atomowości Wdrożenia:
Wdrażamy i testujemy **dokładnie jeden komponent naraz**:
1. Napisanie kodu sieciowego dla pojedynczego komponentu w C++.
2. Kompilacja i uruchomienie weryfikacji w edytorze w 2 instancjach (Server + Client).
3. Potwierdzenie poprawności zachowania obu perspektyw.
4. Dopiero po zatwierdzeniu przejście do następnego komponentu.

### Procedura Uruchomienia Testu w Unreal Editor (PIE):
Do testów Co-op **nie jest potrzebne żadne lobby ani menu główne**:
1. Obok przycisku **Play** w edytorze kliknij ikonę trzech kropek `...` (Multiplayer Options).
2. Ustawienia:
   * **Number of Players:** `2` (lub więcej)
   * **Net Mode:** `Play As Listen Server`
3. Kliknij **Play**:
   * Unreal uruchomi na ekranie **dwa osobne okna gry**.
   * **Okno 1 (Listen Server):** Postać gracza-hosta posiadająca bezpośredni autorytet serwera.
   * **Okno 2 (Client 1):** Postać gracza-klienta połączona przez lokalną pętlę sieciową `127.0.0.1`.
4. Przełączanie między oknami odbywa się kliknięciem myszki. Obaj gracze widzą się nawzajem w czasie rzeczywistym.

---

## 6. Checklista Weryfikacji Nowej Funkcji (Netcode Checklist)

Przed zakończeniem implementacji jakiegokolwiek komponentu lub aktora zadaj sobie 6 pytań:
- [ ] **Authority:** Czy jakakolwiek logika mutująca stan gry może wykonać się na kliencie bez wiedzy serwera? (Musi być: NIE, używaj `REQUIRE_AUTHORITY()`).
- [ ] **Bandwidth:** Czy replikuję zmienne, które zmieniają się co klatkę (np. timery, prędkości)? (Musi być: NIE, użyj `ServerEndTime` lub estymacji).
- [ ] **Quantization:** Czy w `ReplicatedMovement` włączono kwantyzację pozycji i rotacji (`RoundTwoDecimals`, `ByteComponents`)? (Musi być: TAK).
- [ ] **Containers:** Czy żadne pole `UPROPERTY(Replicated)` nie jest `TMap` ani `TSet`? (Musi być: TAK, tylko `TArray` lub proste typy).
- [ ] **Physics:** Czy podnoszone lub przenoszone obiekty fizyczne nie generują konfliktów w solverze Chaosu? (Musi być: TAK, zastosowano Kinematic Follow ze sweepem i Anti-Bulldozer clampem).
- [ ] **Cosmetics:** Czy efekty cząsteczkowe i dźwięki są odpalane po stronie klienta (OnRep/Unreliable RPC), a nie blokują wątku serwera? (Musi być: TAK).

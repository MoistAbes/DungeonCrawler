# Instrukcja Systemowa: Unreal Engine 5 C++ (Java & Enterprise Standards)

Jesteś doświadczonym architektem oprogramowania Java/Spring Boot oraz ekspertem Unreal Engine 5 C++. Twoim celem jest projektowanie i implementowanie systemów gamedevowych z zachowaniem najwyższych standardów inżynierii oprogramowania, czystego kodu oraz wzorców projektowych. Wszystkie koncepcje silnika tłumaczysz przez pryzmat terminologii backendowej (Spring, REST, relacje bazodanowe, Event-Driven Architecture).

---

## 1. Architektura i Wzorce Projektowe (Java/Spring Style)

* **Single Responsibility Principle (Kompozycja zamiast God Object):**
  * Klasy `AActor` i `APawn`/`ACharacter` pełnią wyłącznie rolę punktów wejścia / agregatorów (odpowiednik `@RestController`).
  * Cała logika domenowa (zdrowie, interakcja, walka, fizyka) musi być zamykana w dedykowanych komponentach `UActorComponent` (odpowiednik `@Service`).
* **Dependency Inversion & Loose Coupling:**
  * Komunikacja między odrębnymi obiektami w świecie odbywa się wyłącznie przez interfejsy `UInterface` / `IInterface` (odpowiednik interfejsów Javowych).
  * Całkowity zakaz twardego rzutowania (`Cast<T>`) w kodzie domenowym.
* **Architektura Sterowana Zdarzeniami (Event-Driven):**
  * Komunikacja w górę (komponent -> UI/Animacje/Efekty) realizowana jest przez delegaty dynamiczne (`DECLARE_DYNAMIC_MULTICAST_DELEGATE` – odpowiednik Spring Application Events).
* **Separacja Logiki od Prezentacji (MVC / Layered Pattern):**
  * **C++ (Backend / Domena):** Czysta logika biznesowa, matematyka, autorytatywna obsługa sieci, struktury danych (`USTRUCT`), serwisy i interfejsy.
  * **Blueprints (Frontend / Widok / Prefaby):** Wyłącznie klasy pochodne służące do spinania assetów wizualnych (meshe, animacje, materiały, dźwięki, widgety). Obowiązuje zakaz implementowania złożonej logiki biznesowej w Blueprintach.
* **Data-Driven Configuration:**
  * Parametry fizyczne, mnożniki i progi definiowane są przez struktury konfiguracyjne (`UPROPERTY(EditDefaultsOnly)` – odpowiednik `@ConfigurationProperties`), eliminując sztywne rozgałęzienia warunkowe (`if-ology on type`).

---

## 2. Standardy Integracji Fizyki i Kinematyki (Chaos Physics & Event-Driven Movement)

* **Zero-Tick Velocity Drive (Napęd Różnicowy):**
  * Ruch fizycznego gracza realizowany w 100% przez wektorowe przyspieszenie różnicowe ($F = (V_{\text{target}} - V_{\text{current}}) \cdot K$) w handlerach Enhanced Input bez ciągłego `Tick()`.
  * Hamowanie wyłącznie w płaszczyźnie poziomej ($XY$) – całkowity zakaz sztucznego dławienia grawitacji na osi $Z$ przez globalny `LinearDamping`.
* **Trójwymiarowa Detekcja Podłoża (Sphere Sweep):**
  * Bezwzględny zakaz jednopunktowych `LineTrace` dla detekcji uziemienia. Używamy sferycznego testu objętościowego `SweepSingleByChannel` o promieniu podstawy kapsuły oraz weryfikacji normalnej kąta nachylenia (`ImpactNormal.Z > 0.5f`), zapobiegając blokadom na krawędziach platform.
* **Ujednolicone Obliczanie Energii Kinetycznej:**
  * Odrzucenie surowych impulsów `NormalImpulse` na rzecz czystej prędkości względnej ($v_{\text{rel}}$ w $\text{cm/s}$) rzutowanej na wektor normalnej zderzenia (`DotProduct`).
* **Separacja Kinematyki od Ciał Sztywnych:**
  * Obiekty fizyczne (`Simulate Physics`) muszą blokować schodkowanie postaci (`CanCharacterStepUpOn = ECB_No`) oraz posiadać `FWalkableSlopeOverride`, zapobiegając konfliktom pozycjonowania i eksplozjom sił separujących (*Depenetration Explosions*).
* **Stabilizacja Ciał Ciężkich:**
  * Zastosowanie tłumienia kątowego (`AngularDamping >= 2.0f`, dla gracza `100.0f` z blokadą osi $X/Y/Z$) oraz ciągłej detekcji kolizji (`bUseCCD = true`) na obiektach dynamicznych.

---

## 3. Sieć i Multiplayer (Server-Authoritative First)

* **Autorytatywny Serwer (Source of Truth):**
  * Wszystkie zmiany stanu gry, zadawanie obrażeń i reakcje żywiołowe wykonuje wyłącznie serwer.
* **Intencje Gracza (Client RPC):**
  * Klient wysyła jedynie żądania intencji (`UFUNCTION(Server, Reliable, WithValidation)`).
* **Synchronizacja Stanu (State Replication):**
  * Replikacja danych na klientów za pomocą `UPROPERTY(ReplicatedUsing = OnRep_...)`.

---

## 4. Czysty Kod, Wydajność i Zarządzanie Pamięcią

* **Deskryptywne Nazewnictwo:** Nazwy klas, metod i zmiennych muszą jednoznacznie opisywać ich intencję biznesową.
* **Brak "Magicznych Liczb":** Wszelkie wartości stałe, czasy i mnożniki muszą być polami `UPROPERTY(EditDefaultsOnly, Category = "Config|...")`.
* **Zarządzanie Pamięcią:** Bezwzględny zakaz surowego `new` / `delete`. Pełna integracja z Unreal Garbage Collector (`UObject*`, `UPROPERTY()`) oraz smart pointerami (`TSharedPtr`, `TWeakObjectPtr`).
* **Optymalizacja `Tick()`:** Komponenty mają domyślnie `bCanEverTick = false`. Całość logiki opiera się na zdarzeniach (delegaty, timery, eventy kolizji).

---

## 5. Konwencje Nazewnictwa Unreal Engine (UHT)

* `A` – Aktorzy na scenie (np. `APlayerCharacter`, `AInteractivePropBase`)
* `U` – Obiekty logiki, komponenty i serwisy (np. `UDamageableComponent`, `UInteractionComponent`)
* `I` – Interfejsy biznesowe (np. `IInteractableInterface`, `IStatProviderInterface`)
* `F` – DTO / Struktury konfiguracyjne (np. `FKineticMaterialProperties`)
* `E` – Typy wyliczeniowe / Enums (np. `EPhysicalMaterialType`)

# Plan architektoniczny: Kinematyczny Character Controller + Fizyczne Propy (Dungeon Crawler 4-player co-op)

## Kontekst projektu

- Unreal Engine 5.8, C++ (`MYPROJECT_API`)
- Gra: 4-osobowy kooperacyjny dungeon crawler
- **Nie potrzebujemy** realistycznej symulacji fizyki (brak fluid sim, brak potrzeby resimulacji sieciowej fizyki dla gracza) - potrzebujemy **kontrolowanych, deterministycznych efektów gameplayowych** (odrzuty, rozbijanie przedmiotów, chwytanie/rzucanie obiektów i postaci)
- Docelowo gra ma być sieciowa (multiplayer co-op), ale obecny etap to dokończenie **lokalnego, sensownego modelu ruchu i interakcji** przed podłączeniem networkingu

## Decyzja architektoniczna #1: Gracz i przeciwnicy = KINEMATYCZNI, nie fizyczni

**Problem, który to rozwiązuje:** Wcześniej gracz był w pełni symulowaną bryłą fizyczną (`CapsuleComponent->SetSimulatePhysics(true)`). Powodowało to niekontrolowane zachowania przy kontakcie z propami: wystrzeliwanie gracza/propów przy najmniejszym kontakcie, przewracanie/wystrzeliwanie ciężkich platform pod stojącym graczem. Przyczyna: solver Chaos rozwiązuje kontakt kinematyczne-vs-dynamiczne ciało (nieskończona masa) przez depenetration impulse, co przy złej konfiguracji generuje ekstremalne prędkości.

**Rozwiązanie:** Gracz (i przeciwnicy) przechodzą na model **kinematyczny + ręcznie kontrolowana logika kolizji/odrzutu** - analogiczny do tego, jak działa `UCharacterMovementComponent` pod maską, ale w pełni custom (bo mamy już własny sweep-based grounded check i physics-force-based ruch, który **koncepcyjnie jest dobry i zostaje**, tylko trzeba przełączyć `SimulatePhysics` na `false` i przejść na sweep-move).

**Wspólna baza klas do wprowadzenia:**
```
ACombatCharacterBase (kinematyczny, sweep-move, knockback state, carry state)
├── APlayerCharacter
└── AEnemyCharacterBase (np. AOgreCharacter)
```

Rekomendacja: przenieść przeciwników na tę samą bazę co gracza (nie zostawiać ich jako w pełni symulowanych rigid bodies), żeby AI pathing + knockback + carry system były jednolite i przewidywalne dla całej rodziny "żywych jednostek".

**Propy zostają fizyczne bez zmian** (`AInteractivePropBase`, `SimulatePhysics = true`) - tam realna fizyka ma sens (skrzynki, naczynia, przedmioty do rzucania).

## Decyzja architektoniczna #2: Collision response gracza/przeciwników do propów = Overlap, nie Block

```cpp
CapsuleComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);   // terrain, geometria statyczna
CapsuleComponent->SetCollisionResponseToChannel(ECC_PhysicsProp, ECR_Overlap); // dynamiczne propy - NOWY custom channel
CapsuleComponent->SetGenerateOverlapEvents(true);
```

Wszystkie propy muszą mieć `SetCollisionObjectType(ECC_PhysicsProp)`.

**Efekt:** solver Chaos przestaje w ogóle generować kontakt/impuls między kinematycznym ciałem a propami. Cała logika "co się dzieje przy zetknięciu" jest teraz w 100% w naszym kodzie, zero szansy na niekontrolowany "wybuch" fizyki.

**Ruch po statycznej geometrii** (podłoga, ściany) pozostaje przez `MoveComponent`/`AddActorWorldOffset(..., bSweep=true, &Hit)` - nigdy `SetActorLocation` bez sweep, żeby uniknąć teleportacji w penetrację.

## Decyzja architektoniczna #3: Manualna obsługa kolizji z propami

Rozróżnienie na podstawie masy propa (`HeavyPropMassThreshold`):

- **Lekkie propy** (poniżej progu): overlap → aplikujemy kontrolowaną, **cappowaną** siłę pchania:
  ```cpp
  const float ForceMagnitude = FMath::Min(TouchForceFactor * PropMass * PlayerVelocity.Size2D(), MaxTouchForce);
  OtherComp->AddForceAtLocation(PushDir * ForceMagnitude, ImpactPoint);


```
  `FMath::Min` matematycznie gwarantuje brak "wystrzelenia" niezależnie od geometrii kontaktu.

- **Ciężkie propy/przeszkody** (powyżej progu): traktowane jak ściana - manualna depenetracja (prosta wersja: sfera-vs-sfera push-out w Tick na liście aktualnie nakładających się ciężkich propów; zaawansowana wersja: `ComputePenetration` per-shape - **zacząć od prostej wersji**, doprecyzować tylko jeśli faktycznie zaobserwujemy problem na konkretnych kształtach).

Do zaimplementowania: `TArray<UPrimitiveComponent*> OverlappingHeavyProps` + `OnComponentBeginOverlap`/`EndOverlap` do zarządzania listą + rozwiązywanie w Tick.

## Decyzja architektoniczna #4: Platformy ruchome (stanie na, base tracking)

- Platforma pod stopami **zostaje `Block`** (potrzebne dla sweep-based `IsGrounded()`), ale:
  - `MaxDepenetrationVelocity` ograniczony (np. 200-400 cm/s) globalnie i per-component - **pierwsza, najważniejsza zmiana do zrobienia, bo prawdopodobnie sama rozwiązuje większość "wystrzeleń"**.
  - `bLockXRotation = true; bLockYRotation = true;` na platformach, jeśli nie mają się przechylać pod ciężarem gracza (design decision - w większości przypadków tak).
- **Base tracking** (żeby gracz przemieszczał się z platformą) - ręczne śledzenie delty transformu platformy między tickami i aplikowanie jej do gracza przez `AddActorWorldOffset`. To NIE dzieje się automatycznie przez tarcie w modelu kinematycznym, musi być explicit (analogicznie do `MovementBaseUtility` w CMC).

## Decyzja architektoniczna #5: Unified Knockback System

Jeden system obsługujący WSZYSTKIE przypadki odrzutu: podmuch wiatru (skill), uderzenie młotem (ogr), rzut postacią (throw po carry).

```cpp
// Na ACombatCharacterBase
FVector KnockbackVelocity;
bool bIsKnockedBack;

void ApplyKnockback(const FVector& Impulse); // += do KnockbackVelocity, włącza tick

// W Tick: sweep-move o KnockbackVelocity * DeltaTime, sprawdzenie Hit.bBlockingHit
// -> jeśli ImpactSpeed przekracza próg: DamageableComponent->ApplyKineticImpact()
// -> tłumienie KnockbackVelocity przez FInterpTo do zera
```

Kluczowe: ruch odrzutu też idzie przez **sweep** (nie teleportację), dzięki czemu `Hit.Normal`/`Hit.ImpactNormal` naturalnie daje nam moment i siłę uderzenia w ścianę → obrażenia, bez dodatkowego mechanizmu.

## Decyzja architektoniczna #6: Dwa osobne systemy chwytania - Prop vs Character

**Nie da się użyć `PhysicsHandleComponent` do chwytania postaci** - wymaga on realnego rigid body w solverze, którego kinematyczna postać nie ma. Stąd dwa rozłączne interfejsy/mechanizmy:

### `IGrabbableInterface` (już istnieje, bez zmian) - propy
- `UInteractionComponent` + `UPhysicsHandleComponent` - zostaje jak jest.

### `ICarryableInterface` (NOWY) - postacie (gracz/przeciwnik)
```cpp
class MYPROJECT_API ICarryableInterface
{
public:
    virtual bool CanBeCarried(const AActor* Carrier) const = 0;
    virtual void OnCarryBegin(AActor* Carrier, USceneComponent* HoldSocket) = 0;
    virtual void OnCarryEnd() = 0;
    virtual void OnThrown(const FVector& ThrowVelocity) = 0;
};
```

Implementacja na `ACombatCharacterBase`:
- `OnCarryBegin`: `AttachToComponent` do socketu na carrierze (automatyczne dziedziczenie transformu, brak potrzeby ręcznego tick-sync), blokada inputu ruchu ofiary (`bMovementInputLocked`), collision response na `Ignore` w trakcie noszenia.
- `OnThrown`: `DetachFromActor`, przywrócenie collision, **wywołanie `ApplyKnockback(ThrowVelocity)`** - reużycie systemu z pkt. 5, zero duplikacji kodu.

`UInteractionComponent::PrimaryInteract()` - branching: sprawdź `ICarryableInterface` PRZED `IGrabbableInterface` (priorytet chwytania żywych jednostek nad propami).

**Catch w locie** (jump + catch) - dodatkowy warunek stanu na wejściu do tego samego systemu Carry: `!Target->IsGrounded() && Target->IsBeingKnockedBack()` zamiast nowej infrastruktury fizycznej.

## Decyzja architektoniczna #7: "Fizyczne" efekty specjalne = fake, nie symulacja

Dla wymagań typu "rozbite naczynie → rozlana woda":
- Rozbicie propa: reużycie istniejącego `HandleImpactDamage`/`ImpactSpeed` z progiem (`ShatterImpactThreshold`) do wykrycia "czy uderzenie było wystarczająco silne, żeby rozbić".
- "Rozlewanie cieczy": **NIE symulować hydrodynamiki**. Prosty aktor z rosnącym `UDecalComponent` (promień w czasie) + `UBoxComponent` jako gameplay trigger (np. "śliska podłoga" - redukcja `AccelerationResponsiveness` lub losowy side-slip). Zero prawdziwej fizyki płynów.

Ta filozofia ("kontrolowany efekt z jasną przyczyną, nie symulacja") ma zastosowanie do każdej podobnej mechaniki w grze.

## Kwestie do zaadresowania w przyszłości (networking) - NIE teraz

Ustalone wcześniej w rozmowie, do zrobienia PO dokończeniu lokalnej mechaniki, w tej kolejności:

1. **Networking propów i przeciwników najpierw** (podstawowa replikacja + RPC + `HasAuthority()` checks) - najniższe ryzyko, dobra nauka fundamentów.
2. **Grab/Release/Throw/Interact jako Server RPC z rewalidacją** (`Server_RequestGrab` itd. - serwer sam re-sprawdza dystans/warunki, nie ufa danym od klienta).
3. **Trzymany prop**: wizualna predykcja lokalnie (kosmetyczny lerp mesh), prawdziwy `PhysicsHandle` i fizyka tylko na serwerze - unikamy RTT lagu bez potrzeby resimulacji.
4. **Postać gracza - networking jest teraz ŁATWIEJSZY niż przy physics-body approach**, bo kinematyczny model + manualna kolizja to dokładnie ten sam model, na którym opiera się dojrzały, dobrze wsparty `UCharacterMovementComponent`-style client prediction + server reconciliation (bez potrzeby experimentalnego `NetworkPhysicsComponent`/Chaos resimulation, który jest zarezerwowany dla przypadków wymagających prawdziwej fizycznej resimulacji, np. pojazdów).
5. Propy swobodne (nietrzymane): zwykła replikacja + `EPhysicsReplicationMode::PredictiveInterpolation`, bez predykcji z inputem.

## Stan obecnych plików (punkt wyjścia)

- `APlayerCharacter` (Pawn, obecnie physics-body, do przerobienia na kinematic) - ma: sweep-based `IsGrounded()`, P-controller ruch przez `AddForce` (`F = (V_target - V_current) * K` - **koncepcja zostaje**, zmienia się tylko sposób aplikacji przy przejściu na kinematic), zoom przez spring arm interp, Enhanced Input, `HandleCapsuleHit` → `DamageableComponent`.
- `UInteractionComponent` (ActorComponent) - trace-based grab/throw/interact przez `UPhysicsHandleComponent`, obsługuje `IGrabbableInterface`/`IInteractableInterface`, zero networkingu obecnie.
- `AInteractivePropBase` (Actor, fully simulated rigid body) - implementuje `IGrabbableInterface` + `IInteractableInterface`, ma `HandleImpactDamage` liczący `ImpactSpeed` z impulsu - **ten mechanizm do reużycia dla systemu rozbijania propów (pkt 7)**.
- Brak jakiegokolwiek networkingu w projekcie na tym etapie (brak `bReplicates`, RPC, authority checks) - to jest świadomie odłożone na później.

## Priorytety najbliższych działań (w kolejności)

1. `MaxDepenetrationVelocity` cap (global + per-component) - quick win, prawdopodobnie eliminuje większość obecnych "wybuchów" nawet przed pełną przebudową.
2. Przełączenie `CapsuleComponent` gracza na kinematic + custom collision channel dla propów (Overlap) + sweep-based movement.
3. Manualna obsługa kolizji z propami (lekkie = push force capped, ciężkie = manual depenetration).
4. Base tracking dla platform.
5. Unified Knockback System (`ApplyKnockback`).
6. Ekstrakcja `ACombatCharacterBase`, decyzja o przeniesieniu przeciwników na kinematic.
7. `ICarryableInterface` + Carry state (attach-based).
8. Dopiero po powyższym: networking (propy/enemies → player → advanced).

# Specyfikacja Techniczna i Plan Implementacji: System Modyfikatorów Ruchu i Wpływu Środowiska (Movement Modifier System)

**Wersja:** 1.0  
**Status:** Zaakceptowany do realizacji  
**Data:** 2026-10-07  
**Lokalizacja modułu:** `Source/MyProject/Shared/Components/MovementModifierComponent/`  

---

## 1. Cel i Założenia Architektoniczne

Celem systemu jest stworzenie w pełni uniwersalnego, opartego na danych (Data-Driven) mechanizmu wpływania na ruch postaci i obiektów w świecie gry. Zastępuje on hardcodowane instrukcje warunkowe (np. `if (bIsOnOil)`) modularną architekturą otwartą na:
* **Modyfikatory Podłoża (`SurfaceModifier`):** Zależne od komórki posadzki, po której aktualnie stąpa aktor lub po której przesuwa się fizyczny prop (np. poślizg na `Oiled`, opór wody na `Wet`, przyszłe bagno czy pajęczyny).
* **Modyfikatory Ciała (`BodyModifier`):** Zależne od statusów żywiołowych i wewnętrznych na ciele postaci (np. spowolnienie `Chilled`, ociężałość, zatrucie, paraliż `Electrified`, unieruchomienie).
* **Pełną reużywalność (Shared):** Komponent nie jest ograniczony do gracza – w identyczny sposób obsługuje przeciwników (AI / NPC) oraz współpracuje z fizycznymi propami Chaos.

---

## 2. Architektura Struktur Danych

### 2.1. Uniwersalna struktura `FMovementModifier`
Lokalizacja: `Source/MyProject/Shared/Components/MovementModifierComponent/MovementModifierTypes.h` (lub `Source/MyProject/Environment/Elements/Data/StatusEffectTypes.h`).

```cpp
USTRUCT(BlueprintType)
struct MYPROJECT_API FMovementModifier
{
    GENERATED_BODY()

    /** Mnożnik maksymalnej prędkości poruszania się (1.0 = 100%, 0.85 = -15%, 1.20 = +20%) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "5.0"))
    float SpeedMultiplier = 1.0f;

    /** Mnożnik przyczepności / sterowności na podłożu (1.0 = normalna, 0.05 = skrajny poślizg / drift) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "5.0"))
    float GroundFrictionMultiplier = 1.0f;

    /** Mnożnik drogi hamowania po odpuszczeniu klawiszy WASD (1.0 = natychmiastowe stanięcie, 0.05 = daleki ślizg) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "5.0"))
    float BrakingDecelerationMultiplier = 1.0f;

    /** Flaga całkowitego unieruchomienia postaci (Root / Paraliż / Sidła) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    bool bImmobilized = false;

    /** Zwraca true, jeśli modyfikator nie wprowadza żadnych zmian do domyślnego ruchu */
    bool IsIdentity() const
    {
        return !bImmobilized
            && FMath::IsNearlyEqual(SpeedMultiplier, 1.0f)
            && FMath::IsNearlyEqual(GroundFrictionMultiplier, 1.0f)
            && FMath::IsNearlyEqual(BrakingDecelerationMultiplier, 1.0f);
    }
};
```

---

### 2.2. Rozszerzenie Konfiguracji Statusów (`FStatusEffectConfig`)
Do istniejącej struktury `FStatusEffectConfig` (edytowanej w C++ oraz DataAsset `StatusEffectConfigAsset`) dodajemy rozdzielenie modyfikatorów:

```cpp
// W FStatusEffectConfig:

/** Modyfikator aplikowany na postacie, gdy fizycznie stoją na komórce powierzchni z tym statusem */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Movement")
FMovementModifier SurfaceMovementModifier;

/** Modyfikator aplikowany na postać przez cały czas trwania tego statusu na jej ciele */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Movement")
FMovementModifier BodyMovementModifier;
```

#### Domyślne wartości w rejestrze:
* **`Oiled`:**
  * `SurfaceMovementModifier`: `GroundFrictionMultiplier = 0.05f`, `BrakingDecelerationMultiplier = 0.05f`, `SpeedMultiplier = 1.0f`.
  * `BodyMovementModifier`: `IsIdentity()` (brak poślizgu na suchym gruncie; służy wyłącznie jako nośnik ognia).
* **`Wet`:**
  * `SurfaceMovementModifier`: `SpeedMultiplier = 0.95f` (delikatny opór wody), tarcie normalne $1.0f$.
  * `BodyMovementModifier`: `IsIdentity()`.
* **`Burning`:**
  * `SurfaceMovementModifier`: `IsIdentity()`.
  * `BodyMovementModifier`: opcjonalnie lekki impuls paniki lub standardowy ruch.

---

## 3. Komponent `MovementModifierComponent`

**Lokalizacja:** `Source/MyProject/Shared/Components/MovementModifierComponent/`  
**Klasa:** `UMovementModifierComponent : public UActorComponent`

### 3.1. Odpowiedzialności i Zasada Działania
1. **Cache parametrów bazowych:**
   * Podczas `BeginPlay()` odczytuje i zapamiętuje z `UCharacterMovementComponent` (CMC) bazowe wartości:
     - `BaseMaxWalkSpeed`
     - `BaseGroundFriction`
     - `BaseBrakingDecelerationWalking`
2. **Event-Driven Agregacja ze statusów ciała:**
   * Nasłuchuje delegatów `OnStatusApplied` oraz `OnStatusRemoved` z [`UStatusEffectComponent`](file:///e:/UE_PROJECTS/MyProject/Source/MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h).
   * Przy zmianie statusu przelicza wypadkowy `BodyModifier`.
3. **Wykrywanie podłoża (Surface Sampling):**
   * Posiada niskokosztowy trace w dół (lub pobiera stan posadzki z CMC `CurrentFloor`).
   * Sprawdza status komórki w [`UDungeonSurfaceSubsystem`](file:///e:/UE_PROJECTS/MyProject/Source/MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h).
   * Jeśli współrzędna komórki uległa zmianie $\rightarrow$ pobiera nowy `SurfaceModifier`.
4. **Kalkulacja Wypadkowa (Matematyka Agregacji):**
   * **Prędkość:** Mnożenie iloczynowe:
     $$\text{EffectiveSpeedMultiplier} = \text{BodySpeedMult} \times \text{SurfaceSpeedMult}$$
   * **Tarcie i Hamowanie:** Zasada najniższego oporu (najbardziej śliski element dominuje):
     $$\text{EffectiveFriction} = \min(\text{BodyFrictionMult}, \text{SurfaceFrictionMult})$$
     $$\text{EffectiveBraking} = \min(\text{BodyBrakingMult}, \text{SurfaceBrakingMult})$$
   * **Unieruchomienie:**
     $$\text{bEffectiveImmobilized} = \text{BodyImmobilized} \lor \text{SurfaceImmobilized} \lor (\text{EffectiveSpeedMultiplier} \le 0.0f)$$
5. **Aplikacja do CMC:**
   * Wprowadza wartości do CMC w sposób idempotentny.
   * W przypadku `bEffectiveImmobilized` ustawia `MaxWalkSpeed = 0.0f` lub blokuje wektor przyspieszenia.
6. **Delegat dla UI / Audio / VFX:**
   ```cpp
   DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMovementModifierChanged, const FMovementModifier&, EffectiveModifier);
   UPROPERTY(BlueprintAssignable, Category = "Custom|Movement")
   FOnMovementModifierChanged OnMovementModifierChanged;
   ```

---

## 4. Obsługa Fizycznych Propów (Skrzynie, Beczki, Głazy)

Propy fizyczne Chaos ([`AInteractivePropBase`](file:///e:/UE_PROJECTS/MyProject/Source/MyProject/Dungeon/Props/InteractivePropBase.h)) nie posiadają CMC. Ich ruchem steruje silnik fizyczny `UPrimitiveComponent`.

### 4.1. Mechanizm ślizgu propa
1. Gdy prop o masie $> 0$ przemieszcza się po komórkach, sprawdza komórkę pod swoim środkiem geometrycznym w `UDungeonSurfaceSubsystem`.
2. Jeśli komórka posiada status ze zredukowanym tarciem (`Oiled`):
   * `UPrimitiveComponent::SetLinearDamping(LowDamping)` (np. $0.05$ zamiast bazowego $0.8$).
3. Gdy prop zjeżdża z oleju na suchą posadzkę:
   * Przywracany jest `DefaultLinearDamping`.
4. **Efekt w grze:** Gracz musi włożyć wysiłek w przepchnięcie 60 kg skrzyni po kamieniu, ale po wepchnięciu jej na plamę oleju skrzynia ślizga się płynnie aż do krawędzi plamy.

---

## 5. Plan Implementacji Krok po Kroku

### Faza 1: Struktury Danych i Konfiguracja
1. Utworzenie nagłówka `MovementModifierTypes.h` w module `Shared`.
2. Dodanie pól `SurfaceMovementModifier` oraz `BodyMovementModifier` do struktury `FStatusEffectConfig` w `StatusEffectTypes.h`.
3. Skonfigurowanie domyślnych wartości dla `Oiled` i `Wet` w rejestrze C++ `ElementalReactionRules.cpp`.

### Faza 2: Implementacja `MovementModifierComponent`
1. Utworzenie klasy `UMovementModifierComponent` w `Source/MyProject/Shared/Components/MovementModifierComponent/`.
2. Implementacja cache wartości bazowych CMC w `BeginPlay()`.
3. Spięcie z `UStatusEffectComponent` (nasłuchiwanie zmian statusów).
4. Implementacja próbkowania podłoża w oparciu o `CurrentFloor` z CMC oraz `UDungeonSurfaceSubsystem::GetCellAtWorldLocation`.
5. Implementacja funkcji `RecalculateEffectiveModifiers()` i aplikacja do CMC.
6. Dodanie delegata `OnMovementModifierChanged`.

### Faza 3: Integracja z `PlayerCharacter` i Testy Postaci
1. Dodanie `UMovementModifierComponent` do `APlayerCharacter`.
2. Weryfikacja w grze:
   - Wejście na plamę oleju $\rightarrow$ spadek tarcia, driftowanie przy skręcaniu, sunięcie po puszczeniu WASD.
   - Zejście z plamy $\rightarrow$ natychmiastowe odzyskanie przyczepności.
   - Przetestowanie statusu na ciele (brak poślizgu na suchym podłożu).

### Faza 4: Integracja z Fizycznymi Propami
1. Dodanie do `AInteractivePropBase` logiki dostosowywania `LinearDamping` w oparciu o stan komórki pod propem.
2. Przetestowanie pchania ciężkiej skrzyni na suchym podłożu vs na zaolejonym.

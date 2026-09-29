#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KineticForceLibrary.generated.h"

class UDamageType;
class UPrimitiveComponent;

/**
 * Konfiguracja parametrów fizyki kinetycznej, zderzeń i mechaniki Punch-Through.
 * Zastępuje sztywne wartości (Magic Numbers) w całym ekosystemie zderzeń.
 */
namespace KineticConfig
{
    /** Waga referencyjna obiektu (kg) odpowiadająca mnożnikowi siły 1.0x */
    constexpr float ReferenceMass = 50.0f;

    /** Minimalny mnożnik skalowania siły uderzenia masą (ochrona przed 0 dmg z małych obiektów) */
    constexpr float MinMassFactor = 0.5f;

    /** Maksymalny mnożnik skalowania siły uderzenia masą (ochrona przed niszczeniem świata wagonami) */
    constexpr float MaxMassFactor = 3.5f;

    /** Koszt pędu (kg*cm/s) potrzebny do rozbicia 1 punktu wytrzymałości (Toughness) przeszkody */
    constexpr float ResistanceCostMultiplier = 200.0f;

    /** Minimalna retencja prędkości przy przebiciu (nawet najcięższe przebicie zostawia 15% prędkości) */
    constexpr float MinPunchRetention = 0.15f;

    /** Maksymalna retencja prędkości przy przebiciu (nawet taran traci przynajmniej 2% prędkości) */
    constexpr float MaxPunchRetention = 0.98f;

    /** Minimalna prędkość lotu/toczenia propa (cm/s), by aktywować Pre-Impact Sweep */
    constexpr float MinFlightSpeedForSweep = 80.0f;

    /** Prędkość wygaszania (cm/s), poniżej której Tick propa zostaje uśpiony (0% CPU) */
    constexpr float RestSpeedThreshold = 30.0f;

    /** Współczynnik tłumienia wirowania po przebiciu ściany (gasi 70% prędkości kątowej) */
    constexpr float PunchAngularDamping = 0.3f;

    /** Dystans minimalny i maksymalny sweepa wyprzedzającego (cm) */
    constexpr float PreImpactSweepMinDist = 20.0f;
    constexpr float PreImpactSweepMaxDist = 80.0f;
    constexpr float PreImpactSweepTimeMultiplier = 1.5f;

    /** Promień sfery sweepu względem obrysu bryły */
    constexpr float PreImpactRadiusRatio = 0.75f;
    constexpr float PreImpactRadiusMin = 15.0f;
    constexpr float PreImpactRadiusMax = 60.0f;
}

/**
 * Biblioteka funkcji pomocniczych do aplikowania sił kinetycznych, wybuchów i odrzutów środowiskowych.
 */
UCLASS()
class MYPROJECT_API UKineticForceLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Oblicza współczynnik skalowania obrażeń masą w oparciu o KineticConfig */
    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    static float CalculateMassFactor(float Mass);

    /** Dynamiczne wyliczenie zachowania pędu (Retention) na bazie relacji Pędu do Wytrzymałości przeszkody */
    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    static float CalculatePunchThroughRetention(float BreakerMass, float BreakerSpeed, float ObstacleToughness, float BaseRetention = 0.85f);

    /** Pobiera efektywną masę encji (uwzględnia CharacterMovementComponent dla postaci lub PrimitiveComponent dla propa) */
    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    static float GetEntityMass(const AActor* Actor, const UPrimitiveComponent* Comp);
    /**
     * Oblicza prędkość uderzenia prostopadłego (Closing Speed) pomiędzy dwoma obiektami przy zderzeniu.
     * Uwzględnia fizykę Chaos, ruch postaci (CharacterMovementComponent) oraz geometrię normalnej zderzenia.
     * Zwraca wartość >= 0.0f (0.0f jeśli obiekty oddalają się od siebie).
     */
    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    static float CalculateImpactSpeed(
        const UPrimitiveComponent* SelfComp,
        const AActor* OtherActor,
        const UPrimitiveComponent* OtherComp,
        const FVector& HitNormal);

    /**
     * Zunifikowana obsługa zderzenia kinetycznego dla dowolnej pary obiektów (gracz, prop, ściana).
     * Oblicza prędkości i masy obu ciał, aplikuje obrażenia kinetyczne, a w przypadku zniszczenia celu
     * umożliwia uderzającemu obiektowi przebicie się przez wyrwę (Punch-Through) z zachowaniem części pędu.
     */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    static void HandleKineticImpactAndPunchThrough(
        AActor* InstigatorActor,
        UPrimitiveComponent* InstigatorComp,
        AActor* TargetActor,
        UPrimitiveComponent* TargetComp,
        const FHitResult& Hit,
        const FVector& NormalImpulse = FVector::ZeroVector,
        float PunchThroughRetention = 0.85f);

    /**
     * Sprawdza widoczność celu z punktu wybuchu (Line of Sight / Occlusion).
     * Zwraca true, jeśli promień wybuchu nie jest zablokowany przez litą geometrię (ściany, posadzki).
     * Wypełnia OutHitResult punktem i wektorem normalnym trafienia.
     */
    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    static bool HasExplosionLineOfSight(
        const UWorld* World,
        const FVector& Origin,
        const AActor* TargetActor,
        const UPrimitiveComponent* TargetComp,
        FHitResult& OutHitResult,
        const AActor* IgnoredActor = nullptr);

    /**
     * Aplikuje wybuch radialny: zadaje obrażenia przez DamageableComponent i odrzuca przez KnockbackComponent.
     * Uwzględnia geometryczne ekranowanie przeszkodami (LoS Occlusion).
     */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic", meta = (WorldContext = "WorldContextObject"))
    static void ApplyExplosion(
        const UObject* WorldContextObject,
        const FVector& Origin,
        float Radius,
        float BaseDamage,
        float BaseKnockbackForce,
        AActor* InstigatorActor = nullptr,
        TSubclassOf<UDamageType> DamageTypeClass = nullptr,
        bool bDrawDebug = false);

    /**
     * Aplikuje kierunkowy odrzut do pojedynczego celu (np. uderzenie młotem, podmuch powietrza, taran).
     * @param VerticalLiftRatio Wartość 0.0 - 1.0 określająca, jak bardzo wektor ma podbić cel w górę.
     */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    static void ApplyDirectionalKnockback(
        AActor* TargetActor,
        const FVector& Direction,
        float Force,
        float VerticalLiftRatio = 0.35f,
        AActor* InstigatorActor = nullptr);

    /**
     * Aplikuje fizyczną siłę pchania (np. przy zderzeniu postaci z ciałem sztywnym lub przy niesieniu tarczy/propa).
     * Jeśli obiekt mieści się w limicie masy (<= MaxPushableMass), aplikuje rzeczywistą siłę AddForceAtLocation.
     * Jeśli obiekt jest zbyt ciężki (> MaxPushableMass) i podano VelocityStopThreshold > 0, tłumi mikroruchy Chaos.
     * Zwraca true, jeśli pchnięto obiekt, lub false, jeśli obiekt był zbyt ciężki bądź nie symulował fizyki.
     */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    static bool TryApplyPhysicsPush(
        UPrimitiveComponent* HitComp,
        const FHitResult& Hit,
        const FVector& FallbackDirection,
        float PushForce,
        float MaxPushableMass,
        float VelocityStopThreshold = 0.0f);

    /**
     * Tłumi mikroruchy i jitter fizyki Chaos dla ciał sztywnych przekraczających zadany limit masy.
     */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    static void SuppressHeavyPhysicsJitter(
        UPrimitiveComponent* Comp,
        float MaxMassThreshold,
        float VelocityStopThreshold);

    /**
     * Wykonuje proaktywne geometryczne sprawdzenie przestrzeni przed poruszającą się bryłą fizyczną (Pre-Impact Sweep).
     * Wykrywa zniszczalne ściany lochu (ADungeonStructureBase) lub inne podatne propy (AInteractivePropBase),
     * niszczy je i usuwa ich kolizję ZANIM solver Chaosu wygeneruje twarde zderzenie bryły sztywnej.
     * Zapewnia czysty przelot (Punch-Through) przy rzucie, sturlaniu, odrzucie knockbackiem czy wybuchu.
     * @return true, jeśli wykryto przeszkodę i wykonano akcję zniszczenia/punch-through.
     */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    static bool PerformPreImpactSweep(
        AActor* BreakerActor,
        UPrimitiveComponent* BreakerComp,
        float DeltaTime,
        float SpeedThreshold = 80.0f);
};

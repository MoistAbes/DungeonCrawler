#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CombatForceLibrary.generated.h"

class UDamageType;

/**
 * Biblioteka funkcji pomocniczych do aplikowania sił kinetycznych, wybuchów i odrzutów w walce.
 */
UCLASS()
class MYPROJECT_API UCombatForceLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Aplikuje wybuch radialny: zadaje obrażenia przez DamageableComponent i odrzuca przez KnockbackComponent.
     */
    UFUNCTION(BlueprintCallable, Category = "Combat|Forces", meta = (WorldContext = "WorldContextObject"))
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
     * Aplikuje kierunkowy odrzut do pojedynczego celu (np. uderzenie młotem, strzał, podmuch).
     * @param VerticalLiftRatio Wartość 0.0 - 1.0 określająca, jak bardzo wektor ma podbić cel w górę.
     */
    UFUNCTION(BlueprintCallable, Category = "Combat|Forces")
    static void ApplyDirectionalKnockback(
        AActor* TargetActor,
        const FVector& Direction,
        float Force,
        float VerticalLiftRatio = 0.35f,
        AActor* InstigatorActor = nullptr);

    /**
     * Aplikuje siłę przyciągającą w stronę środka (wir / czarna dziura / skill magnetyczny).
     */
    UFUNCTION(BlueprintCallable, Category = "Combat|Forces", meta = (WorldContext = "WorldContextObject"))
    static void ApplyVortexPull(
        const UObject* WorldContextObject,
        const FVector& Center,
        float Radius,
        float PullStrength,
        AActor* InstigatorActor = nullptr,
        bool bDrawDebug = false);
};

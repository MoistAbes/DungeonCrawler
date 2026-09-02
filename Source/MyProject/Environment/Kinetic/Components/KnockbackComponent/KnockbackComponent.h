#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Environment/Kinetic/Enums/KineticEnums.h"
#include "KnockbackComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnKnockbackReceivedSignature,
    const FVector&, AppliedVelocity,
    AActor*, InstigatorActor);

/**
 * Komponent odpowiedzialny za odbieranie i aplikowanie sił kinetycznych / odrzutów (Knockback).
 * Działa uniwersalnie dla postaci opartych o CharacterMovementComponent oraz obiektów symulujących fizykę.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UKnockbackComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UKnockbackComponent();

    /** Aplikuje siłę kierunkową z uwzględnieniem odporności na odrzut */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    void ApplyImpulseForce(
        const FVector& Direction,
        float Force,
        AActor* InstigatorActor = nullptr,
        bool bIgnoreResistance = false);

    /** Bezpośrednio aplikuje wektor prędkości (np. silny cios młotem) */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    void ApplyKnockback(
        const FVector& Velocity,
        bool bOverrideXY = true,
        bool bOverrideZ = true,
        AActor* InstigatorActor = nullptr);

    /** Aplikuje odrzut radialny od danego punktu w przestrzeni */
    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    void ApplyRadialImpulse(
        const FVector& Origin,
        float Radius,
        float Strength,
        EKnockbackFalloff Falloff = EKnockbackFalloff::Linear,
        AActor* InstigatorActor = nullptr);

    // -------------------------------------------------------------------------
    // Gettery i Settery
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    float GetKnockbackResistance() const { return KnockbackResistance; }

    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    void SetKnockbackResistance(float NewResistance)
    {
        KnockbackResistance = FMath::Clamp(NewResistance, 0.0f, 1.0f);
    }

    UFUNCTION(BlueprintPure, Category = "Environment|Kinetic")
    bool IsImmune() const { return bIsImmune; }

    UFUNCTION(BlueprintCallable, Category = "Environment|Kinetic")
    void SetImmune(bool bNewImmune) { bIsImmune = bNewImmune; }

    // -------------------------------------------------------------------------
    // Zdarzenia
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Environment|Kinetic")
    FOnKnockbackReceivedSignature OnKnockbackReceived;

protected:
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Config|Kinetic",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float KnockbackResistance = 0.0f;

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Config|Kinetic")
    bool bIsImmune = false;

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Config|Kinetic",
        meta = (ClampMin = "0.1", ClampMax = "3.0"))
    float AirborneMultiplier = 1.25f;

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Config|Kinetic",
        meta = (ClampMin = "100.0", ClampMax = "10000.0"))
    float MaxAllowedVelocity = 3500.0f;
};

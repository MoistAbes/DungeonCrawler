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
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void ApplyImpulseForce(
        const FVector& Direction,
        float Force,
        AActor* InstigatorActor = nullptr,
        bool bIgnoreResistance = false);

    /** Bezpośrednio aplikuje wektor prędkości (np. silny cios młotem) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void ApplyKnockback(
        const FVector& Velocity,
        bool bOverrideXY = true,
        bool bOverrideZ = true,
        AActor* InstigatorActor = nullptr);

    /** Aplikuje odrzut radialny od danego punktu w przestrzeni */
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void ApplyRadialImpulse(
        const FVector& Origin,
        float Radius,
        float Strength,
        EKnockbackFalloff Falloff = EKnockbackFalloff::Linear,
        AActor* InstigatorActor = nullptr);

    // -------------------------------------------------------------------------
    // Gettery i Settery
    // -------------------------------------------------------------------------

    /** Zwraca współczynnik odporności na odrzut (0.0 = pełny odrzut, 1.0 = całkowita niewrażliwość) */
    UFUNCTION(BlueprintPure, Category = "Custom|Kinetic")
    float GetKnockbackResistance() const { return KnockbackResistance; }

    /** Dynamicznie ustawia odporność na odrzut (wartość od 0.0 do 1.0) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void SetKnockbackResistance(float NewResistance)
    {
        KnockbackResistance = FMath::Clamp(NewResistance, 0.0f, 1.0f);
    }

    /** Czy cel jest całkowicie odporny na jakiekolwiek siły odrzutu */
    UFUNCTION(BlueprintPure, Category = "Custom|Kinetic")
    bool IsImmune() const { return bIsImmune; }

    /** Ustawia całkowitą nietykalność na odrzut */
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void SetImmune(bool bNewImmune) { bIsImmune = bNewImmune; }

    // -------------------------------------------------------------------------
    // Zdarzenia
    // -------------------------------------------------------------------------

    /** Wywoływane natychmiast po zaaplikowaniu siły odrzutu (zwraca nałożoną prędkość oraz sprawcę) */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnKnockbackReceivedSignature OnKnockbackReceived;

protected:
    /** Współczynnik odporności na siły kinetyczne (0.0 = 100% siły, 0.5 = 50% siły, 1.0 = brak odrzutu) */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Kinetic",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float KnockbackResistance = 0.0f;

    /** Całkowita niewrażliwość na odrzut (ignoruje wszelkie impulsy i wybuchy) */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Kinetic")
    bool bIsImmune = false;

    /** Mnożnik siły odrzutu aplikowany, gdy postać znajduje się w powietrzu (np. podczas skoku) */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Kinetic",
        meta = (ClampMin = "0.1", ClampMax = "3.0"))
    float AirborneMultiplier = 1.25f;

    /** Maksymalna dozwolona prędkość odrzutu (cm/s) zabezpieczająca przed wystrzeleniem w kosmos */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Kinetic",
        meta = (ClampMin = "100.0", ClampMax = "10000.0"))
    float MaxAllowedVelocity = 3500.0f;
};

#include "KnockbackComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"

UKnockbackComponent::UKnockbackComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UKnockbackComponent::ApplyImpulseForce(
    const FVector& Direction,
    float Force,
    AActor* InstigatorActor,
    bool bIgnoreResistance)
{
    if (bIsImmune || Force <= 0.0f)
    {
        return;
    }

    const float ResistanceFactor = bIgnoreResistance ? 1.0f : (1.0f - KnockbackResistance);
    if (ResistanceFactor <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    float EffectiveForce = Force * ResistanceFactor;

    // Postacie w powietrzu otrzymują zwiększony pęd (brak tarcia o podłoże)
    if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
        {
            if (CMC->IsFalling())
            {
                EffectiveForce *= AirborneMultiplier;
            }
        }
    }

    const FVector LaunchVelocity = Direction.GetSafeNormal() * EffectiveForce;
    ExecuteLaunch(LaunchVelocity, true, true, InstigatorActor);
}

void UKnockbackComponent::ApplyKnockback(
    const FVector& Velocity,
    bool bOverrideXY,
    bool bOverrideZ,
    AActor* InstigatorActor)
{
    if (bIsImmune || Velocity.IsNearlyZero())
    {
        return;
    }

    const float ResistanceFactor = 1.0f - KnockbackResistance;
    if (ResistanceFactor <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FVector FinalVelocity = Velocity * ResistanceFactor;
    ExecuteLaunch(FinalVelocity, bOverrideXY, bOverrideZ, InstigatorActor);
}

void UKnockbackComponent::ExecuteLaunch(
    const FVector& Velocity,
    bool bOverrideXY,
    bool bOverrideZ,
    AActor* InstigatorActor)
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    // Debounce / Cooldown chroniący przed wielokrotnym odrzutem w sąsiednich klatkach
    if (const UWorld* World = GetWorld())
    {
        const double CurrentTime = World->GetTimeSeconds();
        if ((CurrentTime - LastKnockbackTime) < KnockbackCooldown)
        {
            return;
        }
        LastKnockbackTime = CurrentTime;
    }

    const FVector ClampedVelocity = Velocity.GetClampedToMaxSize(MaxAllowedVelocity);

    if (ACharacter* Character = Cast<ACharacter>(Owner))
    {
        Character->LaunchCharacter(ClampedVelocity, bOverrideXY, bOverrideZ);
        OnKnockbackReceived.Broadcast(ClampedVelocity, InstigatorActor);
    }
    else if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
    {
        if (PrimComp->IsSimulatingPhysics())
        {
            PrimComp->AddImpulse(ClampedVelocity, NAME_None, true);
            OnKnockbackReceived.Broadcast(ClampedVelocity, InstigatorActor);
        }
    }
}

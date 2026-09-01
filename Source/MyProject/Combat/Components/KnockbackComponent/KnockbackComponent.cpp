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

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(Owner))
    {
        UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
        if (CMC && CMC->IsFalling())
        {
            EffectiveForce *= AirborneMultiplier;
        }

        FVector LaunchVelocity = Direction.GetSafeNormal() * EffectiveForce;
        LaunchVelocity = LaunchVelocity.GetClampedToMaxSize(MaxAllowedVelocity);

        Character->LaunchCharacter(LaunchVelocity, true, true);
        OnKnockbackReceived.Broadcast(LaunchVelocity, InstigatorActor);
    }
    else if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
    {
        if (PrimComp->IsSimulatingPhysics())
        {
            const FVector Impulse = Direction.GetSafeNormal() * EffectiveForce;
            PrimComp->AddImpulse(Impulse, NAME_None, true);
            OnKnockbackReceived.Broadcast(Impulse, InstigatorActor);
        }
    }
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

    FVector FinalVelocity = Velocity * ResistanceFactor;
    FinalVelocity = FinalVelocity.GetClampedToMaxSize(MaxAllowedVelocity);

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(Owner))
    {
        Character->LaunchCharacter(FinalVelocity, bOverrideXY, bOverrideZ);
        OnKnockbackReceived.Broadcast(FinalVelocity, InstigatorActor);
    }
    else if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
    {
        if (PrimComp->IsSimulatingPhysics())
        {
            PrimComp->AddImpulse(FinalVelocity, NAME_None, true);
            OnKnockbackReceived.Broadcast(FinalVelocity, InstigatorActor);
        }
    }
}

void UKnockbackComponent::ApplyRadialImpulse(
    const FVector& Origin,
    float Radius,
    float Strength,
    EKnockbackFalloff Falloff,
    AActor* InstigatorActor)
{
    if (bIsImmune || Radius <= 0.0f || Strength == 0.0f)
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    const FVector OwnerLocation = Owner->GetActorLocation();
    const FVector Delta = OwnerLocation - Origin;
    const float Distance = Delta.Size();

    if (Distance > Radius)
    {
        return;
    }

    float FalloffFactor = 1.0f;
    if (Falloff == EKnockbackFalloff::Linear && Radius > 0.0f)
    {
        FalloffFactor = FMath::Clamp(1.0f - (Distance / Radius), 0.0f, 1.0f);
    }

    FVector Direction = Delta.GetSafeNormal();
    if (Direction.IsNearlyZero())
    {
        Direction = FVector::UpVector;
    }

    const float CalculatedForce = Strength * FalloffFactor;
    ApplyImpulseForce(Direction, CalculatedForce, InstigatorActor, false);
}

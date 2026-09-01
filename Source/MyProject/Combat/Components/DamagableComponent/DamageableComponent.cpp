#include "DamageableComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UDamageableComponent::UDamageableComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UDamageableComponent::BeginPlay()
{
    Super::BeginPlay();
    CurrentDurability = FMath::Clamp(InitialDurability, 0.0f, MaxDurability);

    if (bAutoHandleCharacterImpacts)
    {
        if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        {
            Character->LandedDelegate.AddDynamic(this, &UDamageableComponent::HandleCharacterLanded);

            if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
            {
                Capsule->SetNotifyRigidBodyCollision(true);
                Capsule->OnComponentHit.AddDynamic(this, &UDamageableComponent::HandleCharacterHit);
            }
        }
    }
}

void UDamageableComponent::ApplyDamage(float Amount)
{
    if (IsDestroyed() || Amount <= 0.0f) return;

    CurrentDurability = FMath::Clamp(CurrentDurability - Amount, 0.0f, MaxDurability);

    OnHealthChanged.Broadcast(CurrentDurability);
    OnDurabilityChanged.Broadcast(CurrentDurability, MaxDurability);

    UE_LOG(LogTemp, Warning, TEXT("[DamageableService] %s received %.1f dmg | Remaining: %.1f/%.1f"), 
       *GetOwner()->GetName(), Amount, CurrentDurability, MaxDurability);

    if (IsDestroyed())
    {
        OnDestroyed.Broadcast(GetOwner());
    }
}

void UDamageableComponent::ApplyKineticImpact(float ImpactSpeed)
{
    if (ImpactSpeed <= ImpactSpeedThreshold) return;

    const float ExcessSpeed = ImpactSpeed - ImpactSpeedThreshold;
    const float CalculatedDamage = ExcessSpeed * ImpactDamageMultiplier;

    if (CalculatedDamage >= 1.0f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[KineticService] %s registered impact at Speed: %.1f cm/s | Damage: %.1f"),
            *GetOwner()->GetName(), ImpactSpeed, CalculatedDamage);

        ApplyDamage(CalculatedDamage);
    }
}

void UDamageableComponent::HandleCharacterLanded(const FHitResult& Hit)
{
    if (!bAutoHandleCharacterImpacts)
    {
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
        {
            const float FallSpeed = FMath::Abs(CMC->GetLastUpdateVelocity().Z);
            if (FallSpeed > 0.0f)
            {
                ApplyKineticImpact(FallSpeed);
            }
        }
    }
}

void UDamageableComponent::HandleCharacterHit(
    UPrimitiveComponent* HitComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    FVector NormalImpulse,
    const FHitResult& Hit)
{
    if (!bAutoHandleCharacterImpacts)
    {
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
        {
            if (CMC->IsFalling())
            {
                const FVector IncomingVelocity = CMC->GetLastUpdateVelocity();
                const float ImpactSpeed = -FVector::DotProduct(IncomingVelocity, Hit.ImpactNormal);

                // Reagujemy na zderzenia ze ścianami/przeszkodami (gdzie normalna nie jest płaską podłogą)
                if (ImpactSpeed > 0.0f && FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
                {
                    ApplyKineticImpact(ImpactSpeed);
                }
            }
        }
    }
}

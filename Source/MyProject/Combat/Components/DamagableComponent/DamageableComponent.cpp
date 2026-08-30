#include "DamageableComponent.h"

UDamageableComponent::UDamageableComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UDamageableComponent::BeginPlay()
{
    Super::BeginPlay();
    CurrentDurability = FMath::Clamp(InitialDurability, 0.0f, MaxDurability);
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
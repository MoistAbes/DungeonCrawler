#include "DamageableComponent.h"

#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

UDamageableComponent::UDamageableComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UDamageableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UDamageableComponent, CurrentDurability);
    DOREPLIFETIME(UDamageableComponent, MaxDurability);
}

void UDamageableComponent::BeginPlay()
{
    Super::BeginPlay();

    if (NetUtils::HasAuthority(this))
    {
        CurrentDurability = FMath::Clamp(InitialDurability, 0.0f, MaxDurability);
    }

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

void UDamageableComponent::ApplyDamage(float Amount, EDamageType DamageType, AActor* DamageCauser)
{
    REQUIRE_AUTHORITY();

    // 1. STRAŻNIK: Jeśli obiekt jest niezniszczalny, zniszczony lub Amount <= 0 -> Early Exit (zero kosztu CPU)
    if (bIsInvulnerable || IsDestroyed() || Amount <= 0.0f)
    {
        return;
    }

    // 2. Obliczenie sumarycznej odporności i finalnych obrażeń
    const float TotalResist = GetTotalResistance(DamageType);
    const float FinalDamage = Amount * (1.0f - FMath::Clamp(TotalResist, -1.0f, 1.0f));

    if (FinalDamage <= 0.0f)
    {
        return;
    }

    CurrentDurability = FMath::Clamp(CurrentDurability - FinalDamage, 0.0f, MaxDurability);

    OnHealthChanged.Broadcast(CurrentDurability);
    OnDurabilityChanged.Broadcast(CurrentDurability, MaxDurability);

    UE_LOG(LogDungeonPhysics, Warning, TEXT("[DamageableService]%s %s received %.1f dmg (raw: %.1f, type: %d, resist: %.2f) | Remaining: %.1f/%.1f"), 
       *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), FinalDamage, Amount, static_cast<int32>(DamageType), TotalResist, CurrentDurability, MaxDurability);

    if (IsDestroyed())
    {
        OnDestroyed.Broadcast(GetOwner());
    }
}

float UDamageableComponent::GetTotalResistance(EDamageType DamageType) const
{
    float BaseResist = 0.0f;
    if (const AActor* Owner = GetOwner())
    {
        if (Owner->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
        {
            const EPhysicalMaterialType Mat = IMaterialProviderInterface::Execute_GetMaterialType(Owner);
            BaseResist = PhysicalMaterialUtils::GetBaseResistance(Mat, DamageType);
        }
    }

    const float Modifier = ResistanceModifiers.FindRef(DamageType);
    return BaseResist + Modifier;
}

void UDamageableComponent::SetResistanceModifier(EDamageType DamageType, float Modifier)
{
    ResistanceModifiers.FindOrAdd(DamageType) = Modifier;
}

void UDamageableComponent::ApplyKineticImpact(float ImpactSpeed)
{
    REQUIRE_AUTHORITY();

    if (ImpactSpeed <= ImpactSpeedThreshold)
    {
        return;
    }

    // Debounce / Cooldown chroniący przed powielaniem obrażeń w jednym zderzeniu (Double-Hit)
    if (const UWorld* World = GetWorld())
    {
        const double CurrentTime = World->GetTimeSeconds();
        if ((CurrentTime - LastKineticImpactTime) < KineticImpactCooldown)
        {
            return;
        }
        LastKineticImpactTime = CurrentTime;
    }

    const float ExcessSpeed = ImpactSpeed - ImpactSpeedThreshold;
    const float CalculatedDamage = ExcessSpeed * ImpactDamageMultiplier;

    if (CalculatedDamage >= 1.0f)
    {
        UE_LOG(LogDungeonPhysics, Warning, TEXT("[KineticService]%s %s registered impact at Speed: %.1f cm/s | Damage: %.1f"),
            *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), ImpactSpeed, CalculatedDamage);

        ApplyDamage(CalculatedDamage, EDamageType::Kinetic);
    }
}

void UDamageableComponent::OnRep_CurrentDurability(float OldDurability)
{
    // Klient odbiera zaktualizowaną wartość z serwera i propaguje zdarzenia do UI
    OnHealthChanged.Broadcast(CurrentDurability);
    OnDurabilityChanged.Broadcast(CurrentDurability, MaxDurability);

    if (IsDestroyed() && OldDurability > 0.0f)
    {
        OnDestroyed.Broadcast(GetOwner());
    }
}

void UDamageableComponent::OnRep_MaxDurability(float OldMaxDurability)
{
    OnDurabilityChanged.Broadcast(CurrentDurability, MaxDurability);
}

void UDamageableComponent::HandleCharacterLanded(const FHitResult& Hit)
{
    REQUIRE_AUTHORITY();

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
    REQUIRE_AUTHORITY();

    if (!bAutoHandleCharacterImpacts)
    {
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
        {
            const FVector IncomingVelocity = CMC->GetLastUpdateVelocity();
            const float ImpactSpeed = -FVector::DotProduct(IncomingVelocity, Hit.ImpactNormal);

            // Reagujemy na zderzenia ze ścianami, przeszkodami oraz sufitami (Hit.ImpactNormal.Z <= 0.7f).
            // Ignorujemy płaską podłogę pod stopami (Z > 0.7f), ponieważ lądowanie na stopach
            // jest autorytatywnie obsługiwane przez LandedDelegate.
            if (ImpactSpeed > 0.0f && Hit.ImpactNormal.Z <= 0.7f)
            {
                ApplyKineticImpact(ImpactSpeed);
            }
        }
    }
}

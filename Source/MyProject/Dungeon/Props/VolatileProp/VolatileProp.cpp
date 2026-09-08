#include "VolatileProp.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Environment/Elements/Utilities/ElementalDeliveryLibrary.h"

AVolatileProp::AVolatileProp()
{
    MaterialType = EPhysicalMaterialType::Wood;
    bCanBeGrabbed = true;

    EffectRadius = 600.0f;
    BaseDamage = 25.0f;
    KnockbackForce = 1800.0f;
    bApplyKnockback = true;
    StatusToApply = EStatusEffectType::Burning;
    StatusDuration = 6.0f;
    bDrawDebugRadius = true;
}

void AVolatileProp::HandleOnDestroyed(AActor* DestroyedActor)
{
    REQUIRE_AUTHORITY();

    if (bHasDetonated)
    {
        return;
    }
    bHasDetonated = true;

    const FVector DetonationCenter = GetActorLocation();

    // 1. Rozsyłamy niezawodne powiadomienie kosmetyczne (FX, dźwięk, debug) do wszystkich połączonych graczy
    Multicast_PlayExplosionEffects(DetonationCenter);
    ForceNetUpdate();

    // 2. Eksplozja kinetyczna i statusowa z Line of Sight (Archetyp: Radial Burst)
    const float AppliedKnockback = bApplyKnockback ? KnockbackForce : 0.0f;
    UElementalDeliveryLibrary::ApplyRadialBurst(
        this,
        DetonationCenter,
        EffectRadius,
        StatusToApply,
        StatusDuration,
        this,
        BaseDamage,
        AppliedKnockback);

    // 3. Dla substancji ciekłych lub ognia tworzymy jednolitą strefę kałuży/pożaru (Archetyp: Status Zone)
    if (UElementalChemistryLibrary::IsLiquidStatus(StatusToApply) || StatusToApply == EStatusEffectType::Burning)
    {
        FVector GroundLocation = DetonationCenter;
        FHitResult FloorHit;
        FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(HazardFloorTrace), false, this);
        if (GetWorld() && GetWorld()->LineTraceSingleByChannel(FloorHit, DetonationCenter, DetonationCenter - FVector(0.0f, 0.0f, 500.0f), ECC_Visibility, FloorParams))
        {
            GroundLocation = FloorHit.ImpactPoint + FVector(0.0f, 0.0f, 3.0f);
        }

        UElementalDeliveryLibrary::SpawnStatusZone(
            this,
            GroundLocation,
            EffectRadius,
            StatusToApply,
            StatusDuration,
            EStatusZoneShapeMode::SurfaceDisk,
            FVector::UpVector,
            this);
    }

    UE_LOG(LogDungeonElements, Warning, TEXT("[VolatileProp]%s %s detonated at %s (Status: %s)"),
        *NetUtils::GetNetRolePrefix(this), *GetName(), *DetonationCenter.ToString(), *UEnum::GetValueAsString(StatusToApply));

    Super::HandleOnDestroyed(DestroyedActor);
}

void AVolatileProp::Multicast_PlayExplosionEffects_Implementation(const FVector& DetonationCenter)
{
    // Odtwarzane u wszystkich połączonych klientów oraz na serwerze
    if (bDrawDebugRadius && GetWorld())
    {
        DrawDebugSphere(GetWorld(), DetonationCenter, EffectRadius, 24, FColor::Orange, false, 2.0f, 0, 1.5f);
    }

    // Tutaj wpięte zostaną UNiagaraFunctionLibrary::SpawnSystemAtLocation oraz UGameplayStatics::PlaySoundAtLocation
}

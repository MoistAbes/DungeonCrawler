#include "VolatileProp.h"

#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"

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
    if (bHasDetonated)
    {
        return;
    }
    bHasDetonated = true;

    const FVector DetonationCenter = GetActorLocation();
    UWorld* World = GetWorld();

    // 1. Fizyczna eksplozja kinetyczna (obrażenia i odrzut)
    if (BaseDamage > 0.0f || (bApplyKnockback && KnockbackForce > 0.0f))
    {
        const float AppliedKnockback = bApplyKnockback ? KnockbackForce : 0.0f;
        UKineticForceLibrary::ApplyExplosion(
            this,
            DetonationCenter,
            EffectRadius,
            BaseDamage,
            AppliedKnockback,
            this,
            nullptr,
            bDrawDebugRadius);
    }

    // 2. Aplikowanie statusu żywiołowego w promieniu wybuchu
    if (StatusToApply != EStatusEffectType::None && World)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionShape SphereShape = FCollisionShape::MakeSphere(EffectRadius);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VolatileStatusDetonation), false, this);

        // Wykrywamy graczy, AI i propy w zasięgu
        World->OverlapMultiByChannel(Overlaps, DetonationCenter, FQuat::Identity, ECC_Pawn, SphereShape, QueryParams);

        TArray<FOverlapResult> DynamicOverlaps;
        World->OverlapMultiByChannel(DynamicOverlaps, DetonationCenter, FQuat::Identity, ECC_WorldDynamic, SphereShape, QueryParams);
        Overlaps.Append(DynamicOverlaps);

        TArray<FOverlapResult> PhysicsOverlaps;
        World->OverlapMultiByChannel(PhysicsOverlaps, DetonationCenter, FQuat::Identity, ECC_PhysicsBody, SphereShape, QueryParams);
        Overlaps.Append(PhysicsOverlaps);

        TArray<FOverlapResult> StaticOverlaps;
        World->OverlapMultiByChannel(StaticOverlaps, DetonationCenter, FQuat::Identity, ECC_WorldStatic, SphereShape, QueryParams);
        Overlaps.Append(StaticOverlaps);

        TSet<AActor*> AffectedActors;
        for (const FOverlapResult& Overlap : Overlaps)
        {
            AActor* HitActor = Overlap.GetActor();
            if (!HitActor || HitActor == this || AffectedActors.Contains(HitActor))
            {
                continue;
            }
            AffectedActors.Add(HitActor);

            if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
            {
                StatusComp->ApplyStatus(StatusToApply, StatusDuration, this);
            }
        }

        if (bDrawDebugRadius)
        {
            DrawDebugSphere(World, DetonationCenter, EffectRadius, 24, FColor::Orange, false, 2.0f, 0, 1.5f);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[VolatileProp] %s detonated at %s (Status: %s)"),
        *GetName(), *DetonationCenter.ToString(), *UEnum::GetValueAsString(StatusToApply));

    Super::HandleOnDestroyed(DestroyedActor);
}

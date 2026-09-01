#include "CombatForceLibrary.h"

#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Combat/Components/KnockbackComponent/KnockbackComponent.h"

void UCombatForceLibrary::ApplyExplosion(
    const UObject* WorldContextObject,
    const FVector& Origin,
    float Radius,
    float BaseDamage,
    float BaseKnockbackForce,
    AActor* InstigatorActor,
    TSubclassOf<UDamageType> DamageTypeClass,
    bool bDrawDebug)
{
    if (!WorldContextObject || Radius <= 0.0f)
    {
        return;
    }

    UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return;
    }

    if (bDrawDebug)
    {
        DrawDebugSphere(World, Origin, Radius, 16, FColor::Orange, false, 2.0f, 0, 1.5f);
    }

    FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
    TArray<FOverlapResult> Overlaps;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CombatExplosion), false);
    if (InstigatorActor)
    {
        QueryParams.AddIgnoredActor(InstigatorActor);
    }

    World->OverlapMultiByChannel(
        Overlaps,
        Origin,
        FQuat::Identity,
        ECC_Pawn,
        SphereShape,
        QueryParams);

    // Szukamy też obiektów fizycznych i dynamicznych
    TArray<FOverlapResult> PhysicsOverlaps;
    World->OverlapMultiByChannel(
        PhysicsOverlaps,
        Origin,
        FQuat::Identity,
        ECC_WorldDynamic,
        SphereShape,
        QueryParams);
    Overlaps.Append(PhysicsOverlaps);

    TSet<AActor*> ProcessedActors;

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* HitActor = Overlap.GetActor();
        if (!HitActor || ProcessedActors.Contains(HitActor))
        {
            continue;
        }
        ProcessedActors.Add(HitActor);

        const float Distance = FVector::Dist(Origin, HitActor->GetActorLocation());
        const float DistanceRatio = FMath::Clamp(1.0f - (Distance / Radius), 0.0f, 1.0f);

        // Obrażenia
        if (BaseDamage > 0.0f)
        {
            if (UDamageableComponent* DamageComp = HitActor->FindComponentByClass<UDamageableComponent>())
            {
                const float ScaledDamage = BaseDamage * DistanceRatio;
                DamageComp->ApplyDamage(ScaledDamage);
            }
        }

        // Odrzut
        if (BaseKnockbackForce > 0.0f)
        {
            if (UKnockbackComponent* KnockbackComp = HitActor->FindComponentByClass<UKnockbackComponent>())
            {
                KnockbackComp->ApplyRadialImpulse(
                    Origin,
                    Radius,
                    BaseKnockbackForce,
                    EKnockbackFalloff::Linear,
                    InstigatorActor);
            }
            else if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
            {
                if (PrimComp->IsSimulatingPhysics())
                {
                    PrimComp->AddRadialImpulse(
                        Origin,
                        Radius,
                        BaseKnockbackForce,
                        ERadialImpulseFalloff::RIF_Linear,
                        true);
                }
            }
        }
    }
}

void UCombatForceLibrary::ApplyDirectionalKnockback(
    AActor* TargetActor,
    const FVector& Direction,
    float Force,
    float VerticalLiftRatio,
    AActor* InstigatorActor)
{
    if (!TargetActor || Force <= 0.0f)
    {
        return;
    }

    FVector FinalDir = Direction.GetSafeNormal();
    if (VerticalLiftRatio > 0.0f)
    {
        FinalDir.Z = FMath::Clamp(FinalDir.Z + VerticalLiftRatio, -1.0f, 1.0f);
        FinalDir.Normalize();
    }

    if (UKnockbackComponent* KnockbackComp = TargetActor->FindComponentByClass<UKnockbackComponent>())
    {
        KnockbackComp->ApplyImpulseForce(FinalDir, Force, InstigatorActor);
    }
    else if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetActor->GetRootComponent()))
    {
        if (PrimComp->IsSimulatingPhysics())
        {
            PrimComp->AddImpulse(FinalDir * Force, NAME_None, true);
        }
    }
}

void UCombatForceLibrary::ApplyVortexPull(
    const UObject* WorldContextObject,
    const FVector& Center,
    float Radius,
    float PullStrength,
    AActor* InstigatorActor,
    bool bDrawDebug)
{
    if (!WorldContextObject || Radius <= 0.0f || PullStrength <= 0.0f)
    {
        return;
    }

    UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return;
    }

    if (bDrawDebug)
    {
        DrawDebugSphere(World, Center, Radius, 16, FColor::Purple, false, 1.0f, 0, 1.5f);
    }

    FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CombatVortex), false);

    World->OverlapMultiByChannel(
        Overlaps,
        Center,
        FQuat::Identity,
        ECC_Pawn,
        SphereShape,
        QueryParams);

    TArray<FOverlapResult> PhysicsOverlaps;
    World->OverlapMultiByChannel(
        PhysicsOverlaps,
        Center,
        FQuat::Identity,
        ECC_WorldDynamic,
        SphereShape,
        QueryParams);
    Overlaps.Append(PhysicsOverlaps);

    TSet<AActor*> ProcessedActors;

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* HitActor = Overlap.GetActor();
        if (!HitActor || ProcessedActors.Contains(HitActor))
        {
            continue;
        }
        ProcessedActors.Add(HitActor);

        const FVector ActorLoc = HitActor->GetActorLocation();
        const FVector PullDirection = (Center - ActorLoc).GetSafeNormal();

        ApplyDirectionalKnockback(
            HitActor,
            PullDirection,
            PullStrength,
            0.1f,
            InstigatorActor);
    }
}

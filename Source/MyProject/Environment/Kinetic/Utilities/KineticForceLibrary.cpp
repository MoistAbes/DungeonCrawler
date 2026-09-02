#include "KineticForceLibrary.h"

#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Environment/Kinetic/Components/KnockbackComponent/KnockbackComponent.h"

void UKineticForceLibrary::ApplyExplosion(
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

    TArray<FOverlapResult> Overlaps;
    FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KineticExplosion), false);
    if (InstigatorActor)
    {
        QueryParams.AddIgnoredActor(InstigatorActor);
    }

    // Wykrywamy obiekty fizyczne oraz postacie
    FCollisionObjectQueryParams ObjectParams;
    ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
    ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

    const bool bHit = World->OverlapMultiByObjectType(
        Overlaps,
        Origin,
        FQuat::Identity,
        ObjectParams,
        SphereShape,
        QueryParams);

    if (!bHit)
    {
        return;
    }

    TSet<AActor*> ProcessedActors;

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* HitActor = Overlap.GetActor();
        if (!HitActor || ProcessedActors.Contains(HitActor))
        {
            continue;
        }
        ProcessedActors.Add(HitActor);

        const FVector TargetLocation = HitActor->GetActorLocation();
        const FVector Delta = TargetLocation - Origin;
        const float Distance = Delta.Size();

        if (Distance > Radius)
        {
            continue;
        }

        // Liniowy spadek siły i obrażeń wraz z odległością od centrum wybuchu
        const float FalloffFactor = FMath::Clamp(1.0f - (Distance / Radius), 0.1f, 1.0f);

        // 1. Obrażenia przez DamageableComponent (jeśli aktor posiada komponent)
        if (BaseDamage > 0.0f)
        {
            if (UDamageableComponent* Damageable = HitActor->FindComponentByClass<UDamageableComponent>())
            {
                const float ScaledDamage = BaseDamage * FalloffFactor;
                Damageable->ApplyDamage(ScaledDamage);
            }
            else
            {
                // Fallback na standardowy system obrażeń Unreal Engine
                UGameplayStatics::ApplyDamage(
                    HitActor,
                    BaseDamage * FalloffFactor,
                    InstigatorActor ? InstigatorActor->GetInstigatorController() : nullptr,
                    InstigatorActor,
                    DamageTypeClass);
            }
        }

        // 2. Odrzut przez KnockbackComponent
        if (BaseKnockbackForce > 0.0f)
        {
            if (UKnockbackComponent* Knockback = HitActor->FindComponentByClass<UKnockbackComponent>())
            {
                FVector KnockbackDir = Delta.GetSafeNormal();
                if (KnockbackDir.IsNearlyZero())
                {
                    KnockbackDir = FVector::UpVector;
                }

                // Dodajemy lekkie podbicie w górę (Upward Bias), by eksplozje ładnie podrywały cele z ziemi
                KnockbackDir.Z = FMath::Clamp(KnockbackDir.Z + 0.35f, 0.1f, 1.0f);
                KnockbackDir.Normalize();

                const float ScaledForce = BaseKnockbackForce * FalloffFactor;
                Knockback->ApplyImpulseForce(KnockbackDir, ScaledForce, InstigatorActor, false);
            }
        }
    }
}

void UKineticForceLibrary::ApplyDirectionalKnockback(
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

    if (UKnockbackComponent* Knockback = TargetActor->FindComponentByClass<UKnockbackComponent>())
    {
        FVector AdjustedDirection = Direction.GetSafeNormal2D();
        if (AdjustedDirection.IsNearlyZero())
        {
            AdjustedDirection = TargetActor->GetActorForwardVector();
        }

        // Dodajemy pionowe uniesienie (Vertical Lift)
        AdjustedDirection.Z = FMath::Clamp(VerticalLiftRatio, 0.0f, 1.0f);
        AdjustedDirection.Normalize();

        Knockback->ApplyImpulseForce(AdjustedDirection, Force, InstigatorActor, false);
    }
}

void UKineticForceLibrary::ApplyVortexPull(
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
        DrawDebugSphere(World, Center, Radius, 16, FColor::Purple, false, 2.0f, 0, 1.5f);
    }

    TArray<FOverlapResult> Overlaps;
    FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KineticVortex), false);
    if (InstigatorActor)
    {
        QueryParams.AddIgnoredActor(InstigatorActor);
    }

    FCollisionObjectQueryParams ObjectParams;
    ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
    ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

    const bool bHit = World->OverlapMultiByObjectType(
        Overlaps,
        Center,
        FQuat::Identity,
        ObjectParams,
        SphereShape,
        QueryParams);

    if (!bHit)
    {
        return;
    }

    TSet<AActor*> ProcessedActors;

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* HitActor = Overlap.GetActor();
        if (!HitActor || ProcessedActors.Contains(HitActor))
        {
            continue;
        }
        ProcessedActors.Add(HitActor);

        if (UKnockbackComponent* Knockback = HitActor->FindComponentByClass<UKnockbackComponent>())
        {
            const FVector TargetLocation = HitActor->GetActorLocation();
            const FVector Delta = Center - TargetLocation; // Wektor skierowany DO środka wiru
            const float Distance = Delta.Size();

            if (Distance > Radius || Distance < 50.0f)
            {
                continue;
            }

            const float FalloffFactor = FMath::Clamp(1.0f - (Distance / Radius), 0.2f, 1.0f);
            const FVector PullDirection = Delta.GetSafeNormal();

            Knockback->ApplyImpulseForce(PullDirection, PullStrength * FalloffFactor, InstigatorActor, false);
        }
    }
}

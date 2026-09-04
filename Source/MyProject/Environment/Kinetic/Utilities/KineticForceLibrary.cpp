#include "KineticForceLibrary.h"

#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Environment/Kinetic/Components/KnockbackComponent/KnockbackComponent.h"

namespace KineticHelpers
{
    static FVector GetEntityVelocity(const AActor* Actor, const UPrimitiveComponent* Comp)
    {
        if (Comp && Comp->IsSimulatingPhysics())
        {
            return Comp->GetPhysicsLinearVelocity();
        }
        if (const ACharacter* Character = Cast<ACharacter>(Actor))
        {
            if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
            {
                return CMC->GetLastUpdateVelocity();
            }
            return Character->GetVelocity();
        }
        if (Actor)
        {
            return Actor->GetVelocity();
        }
        return FVector::ZeroVector;
    }

    static UPrimitiveComponent* GetSimulatingPrimitive(const AActor* Actor)
    {
        if (!Actor)
        {
            return nullptr;
        }

        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
        {
            if (RootPrim->IsSimulatingPhysics())
            {
                return RootPrim;
            }
        }

        TArray<UPrimitiveComponent*> Primitives;
        Actor->GetComponents<UPrimitiveComponent>(Primitives);
        for (UPrimitiveComponent* Prim : Primitives)
        {
            if (Prim && Prim->IsSimulatingPhysics())
            {
                return Prim;
            }
        }

        return nullptr;
    }
}

float UKineticForceLibrary::CalculateImpactSpeed(
    const UPrimitiveComponent* SelfComp,
    const AActor* OtherActor,
    const UPrimitiveComponent* OtherComp,
    const FVector& HitNormal)
{
    const AActor* SelfActor = SelfComp ? SelfComp->GetOwner() : nullptr;
    const FVector SelfVelocity = KineticHelpers::GetEntityVelocity(SelfActor, SelfComp);
    const FVector OtherVelocity = KineticHelpers::GetEntityVelocity(OtherActor, OtherComp);

    // Względna prędkość w osi normalnej zderzenia (prędkość zbliżania się obiektów)
    const FVector RelativeVelocity = SelfVelocity - OtherVelocity;
    const float ClosingSpeed = -FVector::DotProduct(RelativeVelocity, HitNormal);

    return FMath::Max(0.0f, ClosingSpeed);
}

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

    // Wykrywamy postacie oraz obiekty fizyczne i dynamiczne
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

    TSet<AActor*> DamagedActors;

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* HitActor = Overlap.GetActor();
        if (!HitActor || DamagedActors.Contains(HitActor))
        {
            continue;
        }
        DamagedActors.Add(HitActor);

        // Obliczamy odległość od epicentrum do krawędzi obiektu
        const FVector TargetLocation = HitActor->GetActorLocation();
        const float Distance = FVector::Dist(Origin, TargetLocation);
        if (Distance > Radius)
        {
            continue;
        }

        // Współczynnik spadku siły z odległością (liniowy falloff, min. 25% na skraju)
        const float FalloffFactor = FMath::Clamp(1.0f - (Distance / Radius), 0.25f, 1.0f);

        // 1. Zadawanie obrażeń przez DamageableComponent
        if (BaseDamage > 0.0f)
        {
            if (UDamageableComponent* Damageable = HitActor->FindComponentByClass<UDamageableComponent>())
            {
                const float ScaledDamage = BaseDamage * FalloffFactor;
                Damageable->ApplyDamage(ScaledDamage);
            }
        }

        // 2. Aplikowanie odrzutu przez KnockbackComponent lub bezpośrednio na bryłę fizyczną Chaos
        if (BaseKnockbackForce > 0.0f)
        {
            FVector KnockbackDir = (TargetLocation - Origin).GetSafeNormal();
            if (KnockbackDir.IsNearlyZero())
            {
                KnockbackDir = FVector::UpVector;
            }

            // Dodajemy lekkie podbicie w górę (Upward Bias), by eksplozje ładnie podrywały cele z ziemi
            KnockbackDir.Z = FMath::Clamp(KnockbackDir.Z + 0.35f, 0.1f, 1.0f);
            KnockbackDir.Normalize();

            const float ScaledForce = BaseKnockbackForce * FalloffFactor;

            if (UKnockbackComponent* Knockback = HitActor->FindComponentByClass<UKnockbackComponent>())
            {
                Knockback->ApplyImpulseForce(KnockbackDir, ScaledForce, InstigatorActor, false);
            }
            else if (UPrimitiveComponent* PhysComp = KineticHelpers::GetSimulatingPrimitive(HitActor))
            {
                // bVelChange = true zapewnia spójną prędkość odrzutu niezależnie od różnicy mas
                PhysComp->AddImpulse(KnockbackDir * ScaledForce, NAME_None, true);
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

    FVector AdjustedDirection = Direction.GetSafeNormal2D();
    if (AdjustedDirection.IsNearlyZero())
    {
        AdjustedDirection = TargetActor->GetActorForwardVector();
    }

    // Dodajemy pionowe uniesienie (Vertical Lift)
    AdjustedDirection.Z = FMath::Clamp(VerticalLiftRatio, 0.0f, 1.0f);
    AdjustedDirection.Normalize();

    if (UKnockbackComponent* Knockback = TargetActor->FindComponentByClass<UKnockbackComponent>())
    {
        Knockback->ApplyImpulseForce(AdjustedDirection, Force, InstigatorActor, false);
    }
    else if (UPrimitiveComponent* PhysComp = KineticHelpers::GetSimulatingPrimitive(TargetActor))
    {
        PhysComp->AddImpulse(AdjustedDirection * Force, NAME_None, true);
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
    ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

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

        const FVector TargetLocation = HitActor->GetActorLocation();
        const FVector Delta = Center - TargetLocation; // Wektor skierowany DO środka wiru
        const float Distance = Delta.Size();

        if (Distance > Radius || Distance < 50.0f)
        {
            continue;
        }

        const float FalloffFactor = FMath::Clamp(1.0f - (Distance / Radius), 0.2f, 1.0f);
        const FVector PullDirection = Delta.GetSafeNormal();
        const float ScaledPull = PullStrength * FalloffFactor;

        if (UKnockbackComponent* Knockback = HitActor->FindComponentByClass<UKnockbackComponent>())
        {
            Knockback->ApplyImpulseForce(PullDirection, ScaledPull, InstigatorActor, false);
        }
        else if (UPrimitiveComponent* PhysComp = KineticHelpers::GetSimulatingPrimitive(HitActor))
        {
            PhysComp->AddImpulse(PullDirection * ScaledPull, NAME_None, true);
        }
    }
}

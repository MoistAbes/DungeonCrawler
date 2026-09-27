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
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"

namespace KineticHelpers
{
    static FVector GetEntityVelocity(const AActor* Actor, const UPrimitiveComponent* Comp)
    {
        if (const AInteractivePropBase* Prop = Cast<AInteractivePropBase>(Actor))
        {
            const FVector FlightVel = Prop->GetLastFlightVelocity();
            const FVector PhysVel = (Comp && Comp->IsSimulatingPhysics()) ? Comp->GetPhysicsLinearVelocity() : FVector::ZeroVector;
            // Jeśli obiekt posiada zarejestrowaną prędkość lotu sprzed zderzenia i jest ona większa od bieżącej, używamy jej
            if (FlightVel.SizeSquared() > PhysVel.SizeSquared() && !FlightVel.IsNearlyZero())
            {
                return FlightVel;
            }
            if (!PhysVel.IsNearlyZero())
            {
                return PhysVel;
            }
            return FlightVel;
        }
        if (Comp && Comp->IsSimulatingPhysics())
        {
            return Comp->GetPhysicsLinearVelocity();
        }
        if (const ACharacter* Character = Cast<ACharacter>(Actor))
        {
            if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
            {
                const FVector LastUpdateVel = CMC->GetLastUpdateVelocity();
                const FVector CharVel = Character->GetVelocity();
                return (LastUpdateVel.SizeSquared() > CharVel.SizeSquared()) ? LastUpdateVel : CharVel;
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

    /**
     * Zunifikowana metoda aplikowania siły kinetycznej na cel.
     * Przekazuje siłę do dedykowanego UKnockbackComponent (jeśli cel go posiada)
     * lub bezpośrednio do aktywnej bryły fizycznej Chaos.
     */
    static void ApplyKineticImpulse(AActor* TargetActor, const FVector& Direction, float Force, AActor* InstigatorActor)
    {
        if (!TargetActor || Force <= 0.0f)
        {
            return;
        }

        if (UKnockbackComponent* Knockback = TargetActor->FindComponentByClass<UKnockbackComponent>())
        {
            Knockback->ApplyImpulseForce(Direction, Force, InstigatorActor, false);
        }
        else if (UPrimitiveComponent* PhysComp = GetSimulatingPrimitive(TargetActor))
        {
            // bVelChange = true zapewnia spójną prędkość odrzutu niezależnie od różnicy mas
            PhysComp->AddImpulse(Direction * Force, NAME_None, true);
        }
    }
}

#include "MyProject/Logging/DungeonLogCategories.h"

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

void UKineticForceLibrary::HandleKineticImpactAndPunchThrough(
    AActor* InstigatorActor,
    UPrimitiveComponent* InstigatorComp,
    AActor* TargetActor,
    UPrimitiveComponent* TargetComp,
    const FHitResult& Hit,
    const FVector& NormalImpulse,
    float PunchThroughRetention)
{
    if (!InstigatorActor || !TargetActor || InstigatorActor == TargetActor)
    {
        return;
    }

    // 1. Zabezpieczenie przed wyścigami Chaosu (obustronny callback OnComponentHit)
    UDamageableComponent* TargetDamageable = TargetActor->FindComponentByClass<UDamageableComponent>();
    UDamageableComponent* InstigatorDamageable = InstigatorActor->FindComponentByClass<UDamageableComponent>();

    if ((TargetDamageable && TargetDamageable->IsDestroyed()) || (InstigatorDamageable && InstigatorDamageable->IsDestroyed()))
    {
        return;
    }

    // 2. Określamy niszczyciela (Breaker - porusza się szybciej) i cel (Victim)
    AActor* BreakerActor = InstigatorActor;
    UPrimitiveComponent* BreakerComp = InstigatorComp;
    UDamageableComponent* BreakerDamageable = InstigatorDamageable;

    AActor* VictimActor = TargetActor;
    UPrimitiveComponent* VictimComp = TargetComp;
    UDamageableComponent* VictimDamageable = TargetDamageable;

    const FVector VelA = KineticHelpers::GetEntityVelocity(InstigatorActor, InstigatorComp);
    const FVector VelB = KineticHelpers::GetEntityVelocity(TargetActor, TargetComp);

    if (VelB.SizeSquared() > VelA.SizeSquared())
    {
        Swap(BreakerActor, VictimActor);
        Swap(BreakerComp, VictimComp);
        Swap(BreakerDamageable, VictimDamageable);
    }

    const FVector BreakerVelocity = KineticHelpers::GetEntityVelocity(BreakerActor, BreakerComp);
    const FVector VictimVelocity = KineticHelpers::GetEntityVelocity(VictimActor, VictimComp);

    // Masy obu ciał
    float BreakerMass = 80.0f;
    if (const ACharacter* Character = Cast<ACharacter>(BreakerActor))
    {
        if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
        {
            BreakerMass = CMC->Mass > 0.0f ? CMC->Mass : 80.0f;
        }
    }
    else if (BreakerComp)
    {
        BreakerMass = FMath::Max(1.0f, BreakerComp->GetMass());
    }

    // 3. Prędkość uderzenia
    const FVector RelativeVelocity = BreakerVelocity - VictimVelocity;
    const float ClosingSpeed = FMath::Abs(FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal));
    const float BreakerSpeed = BreakerVelocity.Size();
    const float EffectiveImpactSpeed = FMath::Max(ClosingSpeed, BreakerSpeed);

    if (EffectiveImpactSpeed <= 0.0f)
    {
        return;
    }

    // 4. Skalowanie masą niszczącego (np. 500kg głaz uderza mocniej niż 10kg stołek)
    const float MassFactor = FMath::Clamp(BreakerMass / 50.0f, 0.5f, 3.5f);
    const float ScaledImpactDamage = EffectiveImpactSpeed * MassFactor;

    UE_LOG(LogDungeonPhysics, Log, TEXT("[KineticImpact]%s Breaker: %s (Mass: %.1f kg, Speed: %.1f cm/s, Vel: %s) -> Victim: %s (HP: %.1f/%.1f) | Normal: %s | ClosingSpeed: %.1f cm/s | ScaledDamage: %.1f"),
        *NetUtils::GetNetRolePrefix(BreakerActor),
        *BreakerActor->GetName(),
        BreakerMass,
        EffectiveImpactSpeed,
        *BreakerVelocity.ToString(),
        *VictimActor->GetName(),
        VictimDamageable ? VictimDamageable->GetCurrentDurability() : 0.0f,
        VictimDamageable ? VictimDamageable->GetMaxDurability() : 0.0f,
        *Hit.ImpactNormal.ToString(),
        ClosingSpeed,
        ScaledImpactDamage);

    // 5. Aplikujemy obrażenia kinetyczne na cel oraz odrzut/recoil na uderzającego
    if (VictimDamageable)
    {
        VictimDamageable->ApplyKineticImpact(ScaledImpactDamage, BreakerActor);
    }
    if (BreakerDamageable)
    {
        BreakerDamageable->ApplyKineticImpact(EffectiveImpactSpeed, VictimActor);
    }

    // 6. Odpowiedź pędu/odrzutu dla postaci (np. gracz lub potwory z KnockbackComponent)
    if (UKnockbackComponent* TargetKnockback = VictimActor->FindComponentByClass<UKnockbackComponent>())
    {
        FVector KnockbackDir = (VictimActor->GetActorLocation() - BreakerActor->GetActorLocation()).GetSafeNormal();
        if (KnockbackDir.IsNearlyZero())
        {
            KnockbackDir = -Hit.ImpactNormal;
        }
        KnockbackDir.Z = FMath::Clamp(KnockbackDir.Z + 0.25f, 0.1f, 1.0f);
        KnockbackDir.Normalize();
        TargetKnockback->ApplyImpulseForce(KnockbackDir, ScaledImpactDamage, BreakerActor);
    }

    // 7. PUNCH-THROUGH:
    // Jeśli cel uległ zniszczeniu, a uderzający obiekt przetrwał:
    const bool bVictimDestroyed = VictimDamageable && VictimDamageable->IsDestroyed();
    const bool bBreakerAlive = !BreakerDamageable || !BreakerDamageable->IsDestroyed();

    if (bVictimDestroyed && bBreakerAlive)
    {
        // Wyłączamy kolizję zniszczonego celu natychmiast
        if (VictimComp)
        {
            VictimComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        VictimActor->SetActorEnableCollision(false);

        // Dynamiczne wyliczenie zachowania pędu (Dynamic Punch-Through Retention)
        // Bazuje na relacji pędu niszczyciela (Masa x Prędkość) do wytrzymałości przeszkody (Toughness)
        float EffectiveRetention = PunchThroughRetention;

        const float BreakerMomentum = BreakerMass * EffectiveImpactSpeed;
        if (BreakerMomentum > 0.0f)
        {
            float ObstacleToughness = 40.0f;
            if (VictimDamageable)
            {
                ObstacleToughness = VictimDamageable->GetMaxDurability();
            }
            else if (VictimComp)
            {
                ObstacleToughness = VictimComp->GetMass();
            }

            // Przelicznik: Toughness na zapotrzebowanie pędu
            const float ResistanceCost = ObstacleToughness * 200.0f;
            const float VelocityLossRatio = ResistanceCost / BreakerMomentum;

            // Naturalny współczynnik zachowania pędu w granicach [0.20, 0.98]
            const float CalculatedRetention = FMath::Clamp(1.0f - VelocityLossRatio, 0.20f, 0.98f);

            // Jeśli wywołujący przekazał customowy mnożnik różny od domyślnego 0.85f (np. ze struktury),
            // skalujemy wynik
            if (FMath::IsNearlyEqual(PunchThroughRetention, 0.85f, 0.01f))
            {
                EffectiveRetention = CalculatedRetention;
            }
            else
            {
                EffectiveRetention = FMath::Clamp(CalculatedRetention * (PunchThroughRetention / 0.85f), 0.15f, 0.98f);
            }
        }

        const float Retention = FMath::Clamp(EffectiveRetention, 0.1f, 1.0f);

        // Punch-Through: obiekt zachowuje swoją prawdziwą trajektorię lotu sprzed kontaktu
        // bez sztucznego rzutowania na normalną ściany (co zaburzało rzuty pod kątem i kaskady ścian)
        FVector PunchVelocity = BreakerVelocity * Retention;

        // Awaryjny fallback na wypadek zerowej prędkości wejściowej
        if (PunchVelocity.IsNearlyZero())
        {
            FVector PenetrationDir = (VictimActor->GetActorLocation() - BreakerActor->GetActorLocation()).GetSafeNormal();
            if (PenetrationDir.IsNearlyZero())
            {
                PenetrationDir = -Hit.ImpactNormal;
            }
            PunchVelocity = PenetrationDir * (EffectiveImpactSpeed * Retention);
        }

        if (ACharacter* Character = Cast<ACharacter>(BreakerActor))
        {
            Character->LaunchCharacter(PunchVelocity, true, true);
        }
        else if (BreakerComp && BreakerComp->IsSimulatingPhysics())
        {
            BreakerComp->SetPhysicsLinearVelocity(PunchVelocity);
            BreakerComp->SetPhysicsAngularVelocityInDegrees(BreakerComp->GetPhysicsAngularVelocityInDegrees() * 0.5f);
            BreakerComp->WakeRigidBody();

            if (AInteractivePropBase* Prop = Cast<AInteractivePropBase>(BreakerActor))
            {
                Prop->SetLastFlightVelocity(PunchVelocity);
            }
        }

        UE_LOG(LogDungeonPhysics, Warning, TEXT("[KineticLibrary]%s [PunchThrough] %s shattered %s! | PreImpactVel: %s (Speed: %.1f) | Retention: %.1f%% | PunchVel: %s (Speed: %.1f)"),
            *NetUtils::GetNetRolePrefix(BreakerActor),
            *BreakerActor->GetName(),
            *VictimActor->GetName(),
            *BreakerVelocity.ToString(),
            BreakerVelocity.Size(),
            Retention * 100.0f,
            *PunchVelocity.ToString(),
            PunchVelocity.Size());
    }
    else
    {
        // Jeśli przeszkoda nie uległa zniszczeniu (odbicie lub wyhamowanie), aktualizujemy zarejestrowaną prędkość
        if (AInteractivePropBase* Prop = Cast<AInteractivePropBase>(BreakerActor))
        {
            Prop->SetLastFlightVelocity(BreakerComp && BreakerComp->IsSimulatingPhysics() ? BreakerComp->GetPhysicsLinearVelocity() : FVector::ZeroVector);
        }
    }
}

bool UKineticForceLibrary::HasExplosionLineOfSight(
    const UWorld* World,
    const FVector& Origin,
    const AActor* TargetActor,
    const UPrimitiveComponent* TargetComp,
    FHitResult& OutHitResult,
    const AActor* IgnoredActor)
{
    if (!World || !TargetActor)
    {
        return false;
    }

    FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(ExplosionLineOfSight), false);
    if (IgnoredActor)
    {
        LoSParams.AddIgnoredActor(IgnoredActor);
    }

    // 1. FAST-PATH: Podstawowy punkt (najbliższy punkt kolizji lub środek aktora)
    FVector PrimaryPoint = TargetActor->GetActorLocation();
    if (TargetComp)
    {
        TargetComp->GetClosestPointOnCollision(Origin, PrimaryPoint);
    }

    const FVector PrimaryDir = (PrimaryPoint - Origin).GetSafeNormal();
    const FVector PrimaryTraceEnd = PrimaryPoint + PrimaryDir * 15.0f;

    if (World->LineTraceSingleByChannel(OutHitResult, Origin, PrimaryTraceEnd, ECC_Visibility, LoSParams))
    {
        if (OutHitResult.GetActor() == TargetActor)
        {
            return true;
        }
    }
    else
    {
        // Jeśli promień nie napotkał żadnego blokera, linia wzroku jest w 100% czysta
        OutHitResult.ImpactPoint = PrimaryPoint;
        OutHitResult.ImpactNormal = -PrimaryDir;
        return true;
    }

    // 2. MULTI-POINT PROBE: Jeśli punkt centralny został zablokowany przez przeszkodę (np. mały prop lub wąski słupek),
    // badamy strategiczne punkty sylwetki celu (głowa, stopy, lewy i prawy bok), by sprawdzić czy cel wystaje zza osłony.
    float CollisionRadius = 0.0f;
    float CollisionHalfHeight = 0.0f;
    TargetActor->GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);

    // Jeśli cel nie posiada wymiarów cylindra (lub jest mikroskopijny), sprawdzamy Bounding Box
    if (CollisionHalfHeight <= 10.0f || CollisionRadius <= 5.0f)
    {
        FVector OriginBox, ExtentBox;
        TargetActor->GetActorBounds(true, OriginBox, ExtentBox);
        CollisionRadius = FMath::Max(ExtentBox.X, ExtentBox.Y);
        CollisionHalfHeight = ExtentBox.Z;
    }

    if (CollisionHalfHeight > 10.0f)
    {
        const FVector Center = TargetActor->GetActorLocation();
        const FVector ToTarget2D = (Center - Origin).GetSafeNormal2D();
        const FVector RightPerp = FVector(-ToTarget2D.Y, ToTarget2D.X, 0.0f);

        // Punkty próbkowania wzdłuż anatomii celu
        TArray<FVector, TInlineAllocator<4>> ProbePoints;
        // A. Głowa / górna część klatki piersiowej (70% wysokości nad środkiem) - chroni przed niskimi skrzynkami/propami
        ProbePoints.Add(Center + FVector(0.0f, 0.0f, CollisionHalfHeight * 0.70f));
        // B. Lewe ramię / flanka (70% promienia w bok) - chroni przed cienkimi słupkami
        ProbePoints.Add(Center - RightPerp * (CollisionRadius * 0.70f));
        // C. Prawe ramię / flanka (70% promienia w bok)
        ProbePoints.Add(Center + RightPerp * (CollisionRadius * 0.70f));
        // D. Stopy / dół sylwetki (70% wysokości pod środkiem) - wykrywa cele nad wybuchem / na schodach
        ProbePoints.Add(Center - FVector(0.0f, 0.0f, CollisionHalfHeight * 0.70f));

        for (const FVector& ProbePoint : ProbePoints)
        {
            const FVector ProbeDir = (ProbePoint - Origin).GetSafeNormal();
            const FVector ProbeEnd = ProbePoint + ProbeDir * 15.0f;

            FHitResult ProbeHit;
            if (World->LineTraceSingleByChannel(ProbeHit, Origin, ProbeEnd, ECC_Visibility, LoSParams))
            {
                if (ProbeHit.GetActor() == TargetActor)
                {
                    OutHitResult = ProbeHit;
                    return true;
                }
            }
            else
            {
                // Promień dotarł do celu bez żadnej kolizji po drodze
                OutHitResult.ImpactPoint = ProbePoint;
                OutHitResult.ImpactNormal = -ProbeDir;
                return true;
            }
        }
    }

    // Wszystkie punkty anatomiczne zostały w pełni zasłonięte przez przeszkody
    return false;
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

    // Wykrywamy postacie, obiekty fizyczne, dynamiczne oraz niszczalne struktury statyczne
    FCollisionObjectQueryParams ObjectParams;
    ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
    ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
    ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

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

        // Filtrujemy geometrię statyczną: ignorujemy stałe elementy lochu bez komponentu zniszczeń
        if (Overlap.GetComponent() && Overlap.GetComponent()->GetCollisionObjectType() == ECC_WorldStatic)
        {
            if (!HitActor->FindComponentByClass<UDamageableComponent>())
            {
                continue;
            }
        }

        // Geometryczne ekranowanie przeszkodami (Line of Sight)
        FHitResult LoSHit;
        if (!HasExplosionLineOfSight(World, Origin, HitActor, Overlap.GetComponent(), LoSHit, InstigatorActor))
        {
            continue;
        }

        DamagedActors.Add(HitActor);

        // Obliczamy odległość od epicentrum do punktu uderzenia
        const FVector TargetLocation = LoSHit.ImpactPoint.IsZero() ? HitActor->GetActorLocation() : LoSHit.ImpactPoint;
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
                Damageable->ApplyDamage(ScaledDamage, EDamageType::Kinetic, InstigatorActor);
            }
        }

        // 2. Aplikowanie odrzutu przez zunifikowany helper kinetyczny
        if (BaseKnockbackForce > 0.0f)
        {
            // Pomijamy odrzut fizyczny dla statycznych struktur architektury
            if (!Overlap.GetComponent() || Overlap.GetComponent()->GetCollisionObjectType() != ECC_WorldStatic)
            {
                FVector KnockbackDir = (HitActor->GetActorLocation() - Origin).GetSafeNormal();
                if (KnockbackDir.IsNearlyZero())
                {
                    KnockbackDir = FVector::UpVector;
                }

                // Dodajemy lekkie podbicie w górę (Upward Bias), by eksplozje ładnie podrywały cele z ziemi
                KnockbackDir.Z = FMath::Clamp(KnockbackDir.Z + 0.35f, 0.1f, 1.0f);
                KnockbackDir.Normalize();

                const float ScaledForce = BaseKnockbackForce * FalloffFactor;
                KineticHelpers::ApplyKineticImpulse(HitActor, KnockbackDir, ScaledForce, InstigatorActor);
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

    KineticHelpers::ApplyKineticImpulse(TargetActor, AdjustedDirection, Force, InstigatorActor);
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

        KineticHelpers::ApplyKineticImpulse(HitActor, PullDirection, ScaledPull, InstigatorActor);
    }
}

bool UKineticForceLibrary::TryApplyPhysicsPush(
    UPrimitiveComponent* HitComp,
    const FHitResult& Hit,
    const FVector& FallbackDirection,
    float PushForce,
    float MaxPushableMass,
    float VelocityStopThreshold)
{
    if (!HitComp || !HitComp->IsSimulatingPhysics())
    {
        return false;
    }

    const float PropMass = HitComp->GetMass();
    if (PropMass <= MaxPushableMass)
    {
        // Kierunek pchnięcia w płaszczyźnie poziomej XY (przeciwny do normalnej zderzenia)
        FVector PushDir = -Hit.ImpactNormal;
        PushDir.Z = 0.0f;
        PushDir = PushDir.GetSafeNormal();

        if (PushDir.IsNearlyZero())
        {
            PushDir = FallbackDirection.GetSafeNormal2D();
        }

        HitComp->WakeRigidBody();
        HitComp->AddForceAtLocation(PushDir * PushForce, Hit.ImpactPoint, Hit.BoneName);
        return true;
    }

    // Jeśli obiekt przekracza dopuszczalną masę gracza, tłumimy niepożądane mikroruchy Chaos
    if (VelocityStopThreshold > 0.0f)
    {
        SuppressHeavyPhysicsJitter(HitComp, MaxPushableMass, VelocityStopThreshold);
    }

    return false;
}

void UKineticForceLibrary::SuppressHeavyPhysicsJitter(
    UPrimitiveComponent* Comp,
    float MaxMassThreshold,
    float VelocityStopThreshold)
{
    if (!Comp || !Comp->IsSimulatingPhysics())
    {
        return;
    }

    if (Comp->GetMass() > MaxMassThreshold)
    {
        if (Comp->GetPhysicsLinearVelocity().Size() < VelocityStopThreshold)
        {
            Comp->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Comp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
    }
}

#include "VolatileProp.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Zones/Utilities/StatusZoneLibrary.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"

AVolatileProp::AVolatileProp()
{
    // Konfiguracja domyślna
    EffectRadius = 600.0f;
    ZoneSpawnMode = EVolatileZoneSpawnMode::SurfaceSplash;
    ZoneDuration = 8.0f;
    SurfaceSplashHeight = 25.0f;

    // Zunifikowana konfiguracja efektu i wybuchu
    ZoneEffectConfig.AppliedStatus = EStatusEffectType::Burning;
    ZoneEffectConfig.InstantDamage = 25.0f;
    ZoneEffectConfig.KnockbackForce = 1800.0f;
    ZoneEffectConfig.ContinuousDamagePerSec = 10.0f;

    bDrawDebugRadius = true;
    bHasDetonated = false;
}

void AVolatileProp::HandleOnDestroyed(AActor* DestroyedActor)
{
    REQUIRE_AUTHORITY();

    if (bHasDetonated)
    {
        return;
    }
    bHasDetonated = true;

    // Wyłączamy kolizję umierającego propa natychmiast, by fizycznie nie blokował promieni LoS nowo tworzonej strefy
    if (MeshComponent)
    {
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    const FVector DetonationCenter = GetActorLocation();

    // 1. Rozsyłamy powiadomienie kosmetyczne (FX, dźwięk, debug) do wszystkich graczy
    Multicast_PlayExplosionEffects(DetonationCenter);
    ForceNetUpdate();

    // 2. Wykonanie dokładnie jednego wybranego trybu strefy (czyste testowanie pojedynczych form)
    switch (ZoneSpawnMode)
    {
    case EVolatileZoneSpawnMode::InstantBurstOnly:
        {
            // Tryb 1: Jednorazowy wybuch z LoS w klatce t0 (brak trwałego aktora)
            UStatusZoneLibrary::ApplyInstantBurst(
                this,
                DetonationCenter,
                EffectRadius,
                ZoneEffectConfig,
                ZoneDuration,
                this);
        }
        break;

    case EVolatileZoneSpawnMode::SurfaceSplash:
        {
            // Tryb 2: Powłoka powierzchniowa (posadzka + pobliskie pionowe ściany)
            SpawnSurfaceSplashes(DetonationCenter);
        }
        break;

    case EVolatileZoneSpawnMode::VolumetricZone:
        {
            // Tryb 3: Trójwymiarowa strefa zawieszona w powietrzu (chmura 3D, dym, gaz)
            UStatusZoneLibrary::SpawnVolumetricZone(
                this,
                DetonationCenter,
                EffectRadius,
                ZoneEffectConfig,
                ZoneDuration,
                this);
        }
        break;

    default:
        break;
    }

    UE_LOG(LogDungeonElements, Warning, TEXT("[VolatileProp]%s %s detonated at %s (Mode: %s | Status: %s, InstantDmg: %.1f, ContinuousDmg: %.1f, Knockback: %.1f)"),
        *NetUtils::GetNetRolePrefix(this), *GetName(), *DetonationCenter.ToString(),
        *UEnum::GetValueAsString(ZoneSpawnMode),
        *UEnum::GetValueAsString(ZoneEffectConfig.AppliedStatus),
        ZoneEffectConfig.InstantDamage, ZoneEffectConfig.ContinuousDamagePerSec, ZoneEffectConfig.KnockbackForce);

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

void AVolatileProp::SpawnSurfaceSplashes(const FVector& DetonationCenter)
{
    if (!GetWorld())
    {
        return;
    }

    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VolatilePropSurfaceTrace), false, this);
    TraceParams.AddIgnoredActor(this);

    TArray<FHitResult> SpawnedSurfaces;

    // 1. Główny splash na posadzce (grawitacyjny opad cieczy)
    const float FloorTraceDist = FMath::Max(500.0f, EffectRadius + 100.0f);
    FHitResult FloorHit;
    if (GetWorld()->LineTraceSingleByChannel(FloorHit, DetonationCenter, DetonationCenter - FVector(0.0f, 0.0f, FloorTraceDist), ECC_Visibility, TraceParams))
    {
        AActor* HitActor = FloorHit.GetActor();
        if (HitActor)
        {
            ASurfaceSplashZone* FloorZone = UStatusZoneLibrary::ApplySurfaceSplash(
                this,
                FloorHit,
                EffectRadius,
                SurfaceSplashHeight,
                ZoneEffectConfig,
                ZoneDuration,
                this);

            if (FloorZone)
            {
                TraceParams.AddIgnoredActor(FloorZone);
                SpawnedSurfaces.Add(FloorHit);
            }
        }
    }

    // 2. Wszechkierunkowe skanowanie 3D (sufit, pionowe ściany, skośne rampy 30-45°, zadaszenia)
    TArray<FVector> ScanDirections;
    ScanDirections.Reserve(18);

    // 2a. Pionowo w górę (sufit lochu)
    ScanDirections.Add(FVector(0.0f, 0.0f, 1.0f));

    // 2b. 8 kierunków horyzontalnych (pionowe ściany)
    constexpr int32 NumHorizontal = 8;
    for (int32 i = 0; i < NumHorizontal; ++i)
    {
        const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * (360.0f / static_cast<float>(NumHorizontal)));
        ScanDirections.Add(FVector(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f));
    }

    // 2c. 4 kierunki skośne w dół (Pitch -35 deg) - wykrywanie ramp 30-45° i spadków terenu wokół beczki
    constexpr float PitchDown = -0.5736f;     // sin(-35 deg)
    constexpr float HorizScaleDown = 0.8192f; // cos(-35 deg)
    for (int32 i = 0; i < 4; ++i)
    {
        const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * 90.0f + 22.5f);
        ScanDirections.Add(FVector(FMath::Cos(AngleRad) * HorizScaleDown, FMath::Sin(AngleRad) * HorizScaleDown, PitchDown));
    }

    // 2d. 4 kierunki skośne w górę (Pitch +35 deg) - sklepienia łukowe i zadaszenia
    constexpr float PitchUp = 0.5736f;       // sin(35 deg)
    constexpr float HorizScaleUp = 0.8192f;  // cos(35 deg)
    for (int32 i = 0; i < 4; ++i)
    {
        const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * 90.0f + 22.5f);
        ScanDirections.Add(FVector(FMath::Cos(AngleRad) * HorizScaleUp, FMath::Sin(AngleRad) * HorizScaleUp, PitchUp));
    }

    for (const FVector& RayDir : ScanDirections)
    {
        const FVector TraceEnd = DetonationCenter + RayDir * EffectRadius;

        FHitResult SurfaceHit;
        if (GetWorld()->LineTraceSingleByChannel(SurfaceHit, DetonationCenter, TraceEnd, ECC_Visibility, TraceParams))
        {
            AActor* HitActor = SurfaceHit.GetActor();
            if (!HitActor)
            {
                continue;
            }

            // Jeśli promień trafił w postać lub interaktywny rekwizyt:
            // Obiekt fizycznie blokuje strugę cieczy przed dotarciem do ściany, więc ZAWSZE otrzymuje status!
            if (!UStatusZoneLibrary::IsValidSurfaceTarget(HitActor))
            {
                if (ZoneEffectConfig.AppliedStatus != EStatusEffectType::None)
                {
                    if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
                    {
                        StatusComp->ApplyStatus(ZoneEffectConfig.AppliedStatus, ZoneDuration, this);
                    }
                }
                continue; // Ciecz została zatrzymana na obiekcie i nie leci na ścianę za nim
            }

            // Deduplikacja: sprawdzamy, czy ten punkt nie leży na tej samej płaszczyźnie co już utworzona strefa
            bool bAlreadySplashed = false;
            for (const FHitResult& ExistingHit : SpawnedSurfaces)
            {
                const float NormalDot = FVector::DotProduct(SurfaceHit.ImpactNormal, ExistingHit.ImpactNormal);
                const float PlaneDist = FMath::Abs(FVector::DotProduct(SurfaceHit.ImpactPoint - ExistingHit.ImpactPoint, ExistingHit.ImpactNormal));
                const float DistSq = FVector::DistSquared(SurfaceHit.ImpactPoint, ExistingHit.ImpactPoint);

                // Ta sama orientacja powierzchni, ta sama płaszczyzna geometryczna i odległość w zasięgu strefy
                if (NormalDot > 0.92f && PlaneDist < 25.0f && DistSq < FMath::Square(EffectRadius * 0.85f))
                {
                    bAlreadySplashed = true;
                    break;
                }
            }

            if (bAlreadySplashed)
            {
                continue;
            }

            // Fizyczny promień plamy na powierzchni odpowiadający przecięciu sfery wybuchu z płaszczyzną:
            // R_surface = sqrt(R_effect^2 - Dist^2), z minimalnym progiem 60 cm
            const float DistToSurface = FMath::Clamp(SurfaceHit.Distance, 0.0f, EffectRadius);
            const float SplashRadius = FMath::Max(60.0f, FMath::Sqrt(FMath::Max(0.0f, FMath::Square(EffectRadius) - FMath::Square(DistToSurface))));

            ASurfaceSplashZone* SurfaceZone = UStatusZoneLibrary::ApplySurfaceSplash(
                this,
                SurfaceHit,
                SplashRadius,
                SurfaceSplashHeight,
                ZoneEffectConfig,
                ZoneDuration,
                this);

            if (SurfaceZone)
            {
                TraceParams.AddIgnoredActor(SurfaceZone);
                SpawnedSurfaces.Add(SurfaceHit);
            }
        }
    }
}

#include "VolatileProp.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Zones/Utilities/StatusZoneLibrary.h"

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
    ZoneEffectConfig.MovementSpeedMultiplier = 1.0f;

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
                this);
        }
        break;

    case EVolatileZoneSpawnMode::SurfaceSplash:
        {
            // Tryb 2: Powłoka powierzchniowa (10-30 cm) na podłodze/ścianie podpięta pod geometrię
            FHitResult SurfaceHit;
            FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VolatilePropFloorTrace), false, this);
            if (GetWorld() && GetWorld()->LineTraceSingleByChannel(SurfaceHit, DetonationCenter, DetonationCenter - FVector(0.0f, 0.0f, 500.0f), ECC_Visibility, TraceParams))
            {
                UStatusZoneLibrary::ApplySurfaceSplash(
                    this,
                    SurfaceHit,
                    EffectRadius,
                    SurfaceSplashHeight,
                    ZoneEffectConfig,
                    ZoneDuration,
                    this);
            }
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

#include "VolatileProp.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Zones/Utilities/StatusZoneLibrary.h"
#include "MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"

AVolatileProp::AVolatileProp()
{
    // Konfiguracja domyślna
    EffectRadius = 600.0f;
    ZoneSpawnMode = EVolatileZoneSpawnMode::RadialBurst;
    ZoneDuration = 8.0f;

    // Fizyka i detonacja kinetyczna
    bDetonateOnThrownImpact = true;
    MinImpactSpeedToDetonate = 300.0f;
    MaxSafeDropSpeed = 650.0f;

    // Zunifikowana konfiguracja efektu i wybuchu
    ZoneEffectConfig.AppliedStatus = EStatusEffectType::Burning;
    ZoneEffectConfig.InstantDamage = 25.0f;
    ZoneEffectConfig.KnockbackForce = 1800.0f;
    ZoneEffectConfig.ContinuousDamagePerSec = 10.0f;

    bDrawDebugRadius = true;
    bHasDetonated = false;
    bWasThrown = false;
    bDroppedSafely = false;
}

void AVolatileProp::OnGrabbed(AActor* Grabber)
{
    Super::OnGrabbed(Grabber);
    REQUIRE_AUTHORITY();

    bWasThrown = false;
    bDroppedSafely = false;
}

void AVolatileProp::OnDropped(AActor* Dropper, const FVector& LaunchVelocity)
{
    Super::OnDropped(Dropper, LaunchVelocity);
    REQUIRE_AUTHORITY();

    const bool bHasThrowForce = (LaunchVelocity.SizeSquared() > FMath::Square(300.0f));
    bWasThrown = bHasThrowForce;
    bDroppedSafely = !bHasThrowForce;
}

void AVolatileProp::HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                     UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                     const FHitResult& Hit)
{
    REQUIRE_AUTHORITY();

    if (!MeshComponent || CarryingActor != nullptr || !OtherActor || OtherActor == this || bHasDetonated)
    {
        return;
    }

    // Jeśli uderzający aktor jest aktualnie trzymany przez postać - ignorujemy ocieranie w dłoniach
    if (const IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(OtherActor))
    {
        if (Grabbable->IsGrabbed())
        {
            return;
        }
    }

    // 1. Obliczamy efektywną prędkość zderzenia (kombinacja prędkości względnych oraz impulsu fizycznego Chaos)
    const float KineticImpactSpeed = UKineticForceLibrary::CalculateImpactSpeed(MeshComponent, OtherActor, OtherComp, Hit.ImpactNormal);
    const float ImpulseSpeed = NormalImpulse.Size() / FMath::Max(1.0f, GetMass());
    const float EffectiveImpactSpeed = FMath::Max(KineticImpactSpeed, ImpulseSpeed);

    // 2. Obsługa pierwszego lądowania po upuszczeniu klawiszem E (bezpieczny spadek pod nogi)
    if (bDroppedSafely)
    {
        bDroppedSafely = false;
        // Jeśli upadek był z bezpiecznej wysokości pod nogi (np. < 650 cm/s), amortyzujemy uderzenie i nie detonujemy
        if (EffectiveImpactSpeed <= MaxSafeDropSpeed)
        {
            Super::HandleImpactDamage(HitComponent, OtherActor, OtherComp, NormalImpulse, Hit);
            return;
        }
    }

    // 3. Weryfikacja rzutu klawiszem R (zużywamy flagę przy pierwszym zderzeniu)
    const bool bIsThrownImpact = bWasThrown;
    bWasThrown = false;

    // 4. Warunki natychmiastowej detonacji:
    // a) Celowy rzut gracza (klawisz R) przy uderzeniu w ścianę/podłogę/przeszkodę/postać
    // b) Zderzenie kinetyczne (rzucony kamień, inna detonująca/uderzająca bomba, upadek z dużej wysokości)
    const bool bDetonateFromThrow = bIsThrownImpact && bDetonateOnThrownImpact && (EffectiveImpactSpeed >= 150.0f);
    const bool bDetonateFromKineticHit = (EffectiveImpactSpeed >= MinImpactSpeedToDetonate);

    if (bDetonateFromThrow || bDetonateFromKineticHit)
    {
        LastImpactHit = Hit;
        UE_LOG(LogDungeonElements, Log, TEXT("[VolatileProp]%s Detonation triggered by impact with %s! (Speed: %.1f cm/s | Thrown: %d, KineticHit: %d)"),
            *NetUtils::GetNetRolePrefix(this), *GetNameSafe(OtherActor), EffectiveImpactSpeed, bDetonateFromThrow, bDetonateFromKineticHit);

        if (DamageableComponent)
        {
            DamageableComponent->ApplyDamage(DamageableComponent->GetMaxDurability());
        }
        return;
    }

    // 5. Standardowe lekkie uderzenia, turlanie i ocieranie
    Super::HandleImpactDamage(HitComponent, OtherActor, OtherComp, NormalImpulse, Hit);
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
    case EVolatileZoneSpawnMode::RadialBurst:
        {
            // Tryb 1: Wszechkierunkowy wybuch 3D z LoS (obrażenia, odrzut, obryzganie ścian/podłóg w siatce)
            UStatusZoneLibrary::ApplyRadialBurst(
                this,
                DetonationCenter,
                EffectRadius,
                ZoneEffectConfig,
                ZoneDuration,
                this);
        }
        break;

    case EVolatileZoneSpawnMode::PointImpact:
        {
            // Tryb 2: Uderzenie punktowe w pojedynczą powierzchnię (np. rzucona butelka, ampułka, koktajl)
            FHitResult HitToUse = LastImpactHit;
            if (!HitToUse.bBlockingHit && GetWorld())
            {
                FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VolatilePropPointTrace), false, this);
                TraceParams.AddIgnoredActor(this);
                GetWorld()->LineTraceSingleByChannel(
                    HitToUse,
                    DetonationCenter,
                    DetonationCenter - FVector(0.0f, 0.0f, EffectRadius + 100.0f),
                    ECC_Visibility,
                    TraceParams);
            }

            UStatusZoneLibrary::ApplyPointImpact(
                this,
                HitToUse,
                EffectRadius,
                ZoneEffectConfig.AppliedStatus,
                ZoneDuration,
                ZoneEffectConfig.InstantDamage,
                this);
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
        UE_LOG(LogDungeonElements, Error, TEXT("[VolatileProp]%s Unhandled or invalid ZoneSpawnMode (%d) on %s! Detonation aborted."),
            *NetUtils::GetNetRolePrefix(this), static_cast<int32>(ZoneSpawnMode), *GetName());
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

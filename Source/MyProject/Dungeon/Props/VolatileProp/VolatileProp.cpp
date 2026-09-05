#include "VolatileProp.h"

#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
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
    REQUIRE_AUTHORITY();

    if (bHasDetonated)
    {
        return;
    }
    bHasDetonated = true;

    const FVector DetonationCenter = GetActorLocation();
    UWorld* World = GetWorld();

    // 1. Rozsyłamy powiadomienie kosmetyczne (FX, dźwięk, debug) do wszystkich graczy
    Multicast_PlayExplosionEffects(DetonationCenter);

    // 2. Fizyczna eksplozja kinetyczna (obrażenia i odrzut) - tylko serwer
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
            false /* serwer nie musi rysować debuga, zrobi to multicast */);
    }

    // 3. Aplikowanie statusu żywiołowego w promieniu wybuchu (Tylko Serwer)
    if (StatusToApply != EStatusEffectType::None && World)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionShape SphereShape = FCollisionShape::MakeSphere(EffectRadius);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VolatileStatusDetonation), false, this);

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
    }

    UE_LOG(LogTemp, Warning, TEXT("[VolatileProp]%s %s detonated at %s (Status: %s)"),
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

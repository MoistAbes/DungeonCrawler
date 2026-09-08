#include "StatusZoneLibrary.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "CollisionQueryParams.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"

AStatusZone* UStatusZoneLibrary::ApplySurfaceSplash(
	const UObject* WorldContextObject,
	const FHitResult& HitResult,
	float SplashRadius,
	float SurfaceHeight,
	const FZoneEffectConfig& EffectConfig,
	float Duration,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || !HitResult.bBlockingHit)
	{
		return nullptr;
	}

	AActor* HitActor = HitResult.GetActor();

	// 1. Jeśli trafiliśmy bezpośrednio w postać z komponentem statusów (np. butelka rozbiła się na głowie gracza)
	if (HitActor)
	{
		if (EffectConfig.AppliedStatus != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(EffectConfig.AppliedStatus, Duration, InstigatorActor);
			}
		}

		// Trafienie w istniejącą strefę
		if (AStatusZone* ExistingZone = Cast<AStatusZone>(HitActor))
		{
			ExistingZone->ApplyElementalHit(EffectConfig.AppliedStatus, 0.0f, InstigatorActor);
			return ExistingZone;
		}
	}

	// 2. Wyliczenie pozycji i orientacji powłoki powierzchniowej
	const FVector SurfaceNormal = HitResult.ImpactNormal.IsNearlyZero() ? FVector::UpVector : HitResult.ImpactNormal.GetSafeNormal();
	const FVector SpawnLocation = HitResult.ImpactPoint + SurfaceNormal * 2.0f;
	const FRotator SpawnRotation = FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = InstigatorActor;
	SpawnParams.Instigator = Cast<APawn>(InstigatorActor);

	AStatusZone* Zone = World->SpawnActor<AStatusZone>(AStatusZone::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
	if (!Zone)
	{
		return nullptr;
	}

	// 3. Przyczepienie strefy do trafionej ściany/podłogi (AttachToComponent)
	// Dzięki temu zburzenie ściany automatycznie niszczy strefę, a ruch taranu przemieszcza plamę!
	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		Zone->AttachToComponent(HitComponent, FAttachmentTransformRules::KeepWorldTransform);
	}

	Zone->InitializeZone(
		EffectConfig,
		SplashRadius,
		Duration,
		EZoneShapeType::SurfaceSplash,
		SurfaceHeight,
		SurfaceNormal,
		InstigatorActor);

	UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Spawned Surface Splash (Radius: %.1f cm, Height: %.1f cm) on %s"),
		SplashRadius, SurfaceHeight, *GetNameSafe(HitActor));

	return Zone;
}

AStatusZone* UStatusZoneLibrary::SpawnVolumetricZone(
	const UObject* WorldContextObject,
	const FVector& Location,
	float Radius,
	const FZoneEffectConfig& EffectConfig,
	float Duration,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = InstigatorActor;
	SpawnParams.Instigator = Cast<APawn>(InstigatorActor);

	AStatusZone* Zone = World->SpawnActor<AStatusZone>(AStatusZone::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Zone)
	{
		return nullptr;
	}

	Zone->InitializeZone(
		EffectConfig,
		Radius,
		Duration,
		EZoneShapeType::VolumetricSphere,
		Radius,
		FVector::UpVector,
		InstigatorActor);

	UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Spawned Volumetric Zone (Radius: %.1f cm, Duration: %.1f s)"), Radius, Duration);

	return Zone;
}

void UStatusZoneLibrary::ApplyInstantBurst(
	const UObject* WorldContextObject,
	const FVector& Origin,
	float Radius,
	const FZoneEffectConfig& EffectConfig,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f)
	{
		return;
	}

	// 1. Fala kinetyczna (wybuch fizyczny + obrażenia) z Line-of-Sight pobierana z EffectConfig
	if (EffectConfig.InstantDamage > 0.0f || EffectConfig.KnockbackForce > 0.0f)
	{
		UKineticForceLibrary::ApplyExplosion(
			WorldContextObject,
			Origin,
			Radius,
			EffectConfig.InstantDamage,
			EffectConfig.KnockbackForce,
			InstigatorActor,
			nullptr,
			false);
	}

	// 2. Aplikowanie statusu do obiektów w zasięgu wzroku wybuchu (LoS)
	if (EffectConfig.AppliedStatus != EStatusEffectType::None)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StatusZoneInstantBurst), false, InstigatorActor);

		World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, SphereShape, QueryParams);

		TArray<FOverlapResult> DynamicOverlaps;
		World->OverlapMultiByChannel(DynamicOverlaps, Origin, FQuat::Identity, ECC_WorldDynamic, SphereShape, QueryParams);
		Overlaps.Append(DynamicOverlaps);

		TArray<FOverlapResult> PhysicsOverlaps;
		World->OverlapMultiByChannel(PhysicsOverlaps, Origin, FQuat::Identity, ECC_PhysicsBody, SphereShape, QueryParams);
		Overlaps.Append(PhysicsOverlaps);

		TSet<AActor*> ProcessedActors;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || HitActor == InstigatorActor || ProcessedActors.Contains(HitActor))
			{
				continue;
			}

			// Inna strefa w pobliżu wybuchu
			if (AStatusZone* OtherZone = Cast<AStatusZone>(HitActor))
			{
				OtherZone->ApplyElementalHit(EffectConfig.AppliedStatus, EffectConfig.InstantDamage, InstigatorActor);
				ProcessedActors.Add(HitActor);
				continue;
			}

			// Weryfikacja Line-of-Sight
			FHitResult LoSHit;
			if (!UKineticForceLibrary::HasExplosionLineOfSight(World, Origin, HitActor, Overlap.GetComponent(), LoSHit, InstigatorActor))
			{
				continue;
			}

			ProcessedActors.Add(HitActor);

			// Aplikacja statusu
			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(EffectConfig.AppliedStatus, 4.0f, InstigatorActor);
			}
		}
	}
}

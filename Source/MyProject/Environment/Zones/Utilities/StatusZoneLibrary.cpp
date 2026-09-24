#include "StatusZoneLibrary.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "CollisionQueryParams.h"
#include "GameFramework/Pawn.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h"



AVolumetricStatusZone* UStatusZoneLibrary::SpawnVolumetricZone(
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

	// Zone Merging: Sprawdzamy, czy w pobliżu istnieje strefa wolumetryczna tego samego żywiołu
	if (EffectConfig.AppliedStatus != EStatusEffectType::None)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape OverlapSphere = FCollisionShape::MakeSphere(Radius * 0.75f);
		FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(StatusZoneMergeQuery), false);

		if (World->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, ECC_WorldDynamic, OverlapSphere, OverlapParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (AVolumetricStatusZone* ExistingVolumetric = Cast<AVolumetricStatusZone>(Overlap.GetActor()))
				{
					if (ExistingVolumetric->GetStatusType() == EffectConfig.AppliedStatus)
					{
						ExistingVolumetric->MergeWithZone(Duration, 1.20f, Radius * 1.5f);
						if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
						{
							SurfaceSubsystem->ApplyElementalBurst(Location, Radius, EffectConfig.AppliedStatus, Duration, InstigatorActor);
						}
						UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Merged into existing Volumetric Zone (Radius: %.1f cm)"), ExistingVolumetric->GetRadius());
						return ExistingVolumetric;
					}
				}
			}
		}
	}

	// 1. Zastosowanie jednorazowego impulsu wybuchu na aktorów w strefie jeśli zdefiniowano InstantDamage lub KnockbackForce
	if (EffectConfig.InstantDamage > 0.0f || EffectConfig.KnockbackForce > 0.0f)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StatusZoneInitialBurst), false);
		if (InstigatorActor)
		{
			QueryParams.AddIgnoredActor(InstigatorActor);
		}

		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
		ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
		ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

		if (World->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, ObjectParams, SphereShape, QueryParams))
		{
			TSet<AActor*> ProcessedActors;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* TargetActor = Overlap.GetActor();
				if (!TargetActor || TargetActor == InstigatorActor || ProcessedActors.Contains(TargetActor))
				{
					continue;
				}
				ProcessedActors.Add(TargetActor);

				if (EffectConfig.InstantDamage > 0.0f)
				{
					if (UDamageableComponent* Damageable = TargetActor->FindComponentByClass<UDamageableComponent>())
					{
						Damageable->ApplyDamage(EffectConfig.InstantDamage);
					}
				}

				if (EffectConfig.KnockbackForce > 0.0f)
				{
					FVector KnockbackDir = (TargetActor->GetActorLocation() - Location).GetSafeNormal();
					if (KnockbackDir.IsNearlyZero())
					{
						KnockbackDir = FVector::UpVector;
					}
					UKineticForceLibrary::ApplyDirectionalKnockback(TargetActor, KnockbackDir, EffectConfig.KnockbackForce, 0.35f, InstigatorActor);
				}
			}
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = InstigatorActor;
	SpawnParams.Instigator = Cast<APawn>(InstigatorActor);

	AVolumetricStatusZone* Zone = World->SpawnActor<AVolumetricStatusZone>(AVolumetricStatusZone::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Zone)
	{
		return nullptr;
	}

	Zone->InitializeVolumetricZone(EffectConfig, Radius, Duration, InstigatorActor);

	UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Spawned Volumetric Zone (Radius: %.1f cm, Duration: %.1f s)"), Radius, Duration);

	return Zone;
}

void UStatusZoneLibrary::ApplyRadialBurst(
	const UObject* WorldContextObject,
	const FVector& Origin,
	float Radius,
	const FZoneEffectConfig& EffectConfig,
	float Duration,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f)
	{
		return;
	}

	// Propagacja impulsu wybuchu na rzadką siatkę komórek powierzchniowych (projekcja 3D na posadzkę, ściany, sufit)
	if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
	{
		SurfaceSubsystem->ApplyElementalBurst(Origin, Radius, EffectConfig.AppliedStatus, Duration, InstigatorActor);
	}

	// 1. Zunifikowany pojedynczy przebieg przestrzenny (Single-Pass Query)
	// Wykrywamy: postacie (Pawn), obiekty fizyczne (PhysicsBody), dynamiczne (WorldDynamic) oraz niszczalne struktury (WorldStatic)
	TArray<FOverlapResult> Overlaps;
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StatusZoneRadialBurst), false);
	if (InstigatorActor)
	{
		QueryParams.AddIgnoredActor(InstigatorActor);
	}

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

	TSet<AActor*> ProcessedActors;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == InstigatorActor || ProcessedActors.Contains(HitActor))
		{
			continue;
		}

		// Filtrowanie statycznej architektury: pomijamy niezniszczalną geometrię lochu bez DamageableComponent
		if (Overlap.GetComponent() && Overlap.GetComponent()->GetCollisionObjectType() == ECC_WorldStatic)
		{
			if (!HitActor->FindComponentByClass<UDamageableComponent>())
			{
				continue;
			}
		}

		// Reakcja łańcuchowa z innymi strefami żywiołów (np. detonacja lub podpalenie plamy)
		if (AStatusZoneBase* OtherZone = Cast<AStatusZoneBase>(HitActor))
		{
			OtherZone->ApplyElementalHit(EffectConfig.AppliedStatus, EffectConfig.InstantDamage, InstigatorActor);
			ProcessedActors.Add(HitActor);
			continue;
		}

		// Geometryczna weryfikacja Line of Sight z wielopunktowym próbnikiem
		FHitResult LoSHit;
		if (!UKineticForceLibrary::HasExplosionLineOfSight(World, Origin, HitActor, Overlap.GetComponent(), LoSHit, InstigatorActor))
		{
			continue;
		}

		ProcessedActors.Add(HitActor);

		// Obliczamy spadek siły z odległością (liniowy falloff, min. 25% na skraju)
		const FVector TargetLocation = LoSHit.ImpactPoint.IsZero() ? HitActor->GetActorLocation() : LoSHit.ImpactPoint;
		const float Distance = FVector::Dist(Origin, TargetLocation);
		if (Distance > Radius)
		{
			continue;
		}
		const float FalloffFactor = FMath::Clamp(1.0f - (Distance / Radius), 0.25f, 1.0f);

		// A. Zadawanie natychmiastowych obrażeń przez DamageableComponent
		if (EffectConfig.InstantDamage > 0.0f)
		{
			if (UDamageableComponent* Damageable = HitActor->FindComponentByClass<UDamageableComponent>())
			{
				const float ScaledDamage = EffectConfig.InstantDamage * FalloffFactor;
				Damageable->ApplyDamage(ScaledDamage);
			}
		}

		// B. Aplikowanie odrzutu fizycznego (Knockback)
		if (EffectConfig.KnockbackForce > 0.0f)
		{
			if (!Overlap.GetComponent() || Overlap.GetComponent()->GetCollisionObjectType() != ECC_WorldStatic)
			{
				FVector KnockbackDir = (HitActor->GetActorLocation() - Origin).GetSafeNormal();
				if (KnockbackDir.IsNearlyZero())
				{
					KnockbackDir = FVector::UpVector;
				}
				const float ScaledForce = EffectConfig.KnockbackForce * FalloffFactor;
				UKineticForceLibrary::ApplyDirectionalKnockback(HitActor, KnockbackDir, ScaledForce, 0.35f, InstigatorActor);
			}
		}

		// C. Aplikacja statusu żywiołowego (na postacie i propy z weryfikacją kompatybilności materiału)
		if (EffectConfig.AppliedStatus != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(EffectConfig.AppliedStatus, Duration, InstigatorActor);
			}
		}
	}
}

void UStatusZoneLibrary::ApplyInstantBurst(
	const UObject* WorldContextObject,
	const FVector& Origin,
	float Radius,
	const FZoneEffectConfig& EffectConfig,
	float Duration,
	AActor* InstigatorActor)
{
	ApplyRadialBurst(WorldContextObject, Origin, Radius, EffectConfig, Duration, InstigatorActor);
}

bool UStatusZoneLibrary::ApplyPointImpact(
	const UObject* WorldContextObject,
	const FHitResult& HitResult,
	float SplashRadius,
	EStatusEffectType StatusType,
	float Duration,
	float DirectDamage,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || !HitResult.bBlockingHit)
	{
		return false;
	}

	AActor* TargetActor = HitResult.GetActor();

	// 1. Uderzenie w istniejący aktor strefy (np. strzała ogniowa w strefę wolumetryczną lub powierzchniową)
	if (AStatusZoneBase* Zone = Cast<AStatusZoneBase>(TargetActor))
	{
		Zone->ApplyElementalHit(StatusType, DirectDamage, InstigatorActor);
		return true;
	}

	// 2. Bezpośrednie obrażenia i status na trafionym celu (postać, prop, struktura niszczalna)
	if (TargetActor)
	{
		if (DirectDamage > 0.0f)
		{
			if (UDamageableComponent* Damageable = TargetActor->FindComponentByClass<UDamageableComponent>())
			{
				Damageable->ApplyDamage(DirectDamage);
			}
		}

		if (StatusType != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = TargetActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(StatusType, Duration, InstigatorActor);
			}
			else if (StatusType == EStatusEffectType::Burning)
			{
				// Obiekty podatne na ogień bez StatusEffectComponent (np. drewniane barykady)
				if (UDamageableComponent* Damageable = TargetActor->FindComponentByClass<UDamageableComponent>())
				{
					EPhysicalMaterialType MatType = EPhysicalMaterialType::Stone;
					if (TargetActor->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
					{
						MatType = IMaterialProviderInterface::Execute_GetMaterialType(TargetActor);
					}

					if (MatType == EPhysicalMaterialType::Wood)
					{
						Damageable->ApplyDamage(25.0f);
					}
				}
			}
		}
	}

	// 3. Pokrycie powierzchni w siatce lochu (posadzka, ściana, sufit)
	if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
	{
		if (StatusType != EStatusEffectType::None && SplashRadius > 0.0f)
		{
			SurfaceSubsystem->PaintSurfaceFromHit(HitResult, SplashRadius, StatusType, Duration, InstigatorActor);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	DrawDebugPoint(World, HitResult.ImpactPoint, 10.0f, FColor::Cyan, false, 2.0f);
	DrawDebugDirectionalArrow(World, HitResult.ImpactPoint, HitResult.ImpactPoint + HitResult.ImpactNormal * 30.0f, 12.0f, FColor::Cyan, false, 2.0f, 0, 2.0f);
#endif

	return true;
}

bool UStatusZoneLibrary::ApplyPointHit(
	AActor* TargetActor,
	const FVector& HitLocation,
	const FVector& HitNormal,
	EStatusEffectType StatusType,
	float Duration,
	AActor* InstigatorActor)
{
	if (!TargetActor || !NetUtils::HasAuthority(TargetActor) || StatusType == EStatusEffectType::None)
	{
		return false;
	}

	FHitResult SyntheticHit;
	SyntheticHit.bBlockingHit = true;
	SyntheticHit.HitObjectHandle = FActorInstanceHandle(TargetActor);
	SyntheticHit.ImpactPoint = HitLocation;
	SyntheticHit.ImpactNormal = HitNormal;
	SyntheticHit.Location = HitLocation;
	SyntheticHit.Normal = HitNormal;

	return ApplyPointImpact(
		TargetActor,
		SyntheticHit,
		45.0f,
		StatusType,
		Duration,
		15.0f,
		InstigatorActor);
}


#include "ElementalDeliveryLibrary.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Environment/Elements/StatusZone/ElementalStatusZone.h"
#include "MyProject/Environment/Elements/Data/StatusEffectDefinitions.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"

bool UElementalDeliveryLibrary::ApplyPointHit(
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

	// 1. Jeśli uderzyliśmy w istniejącą strefę żywiołu (np. płonąca strzała w plamę oleju)
	if (AElementalStatusZone* Zone = Cast<AElementalStatusZone>(TargetActor))
	{
		Zone->ApplyElementalHit(StatusType, 15.0f, InstigatorActor);
		return true;
	}

	// 2. Postać lub prop z komponentem statusów
	if (UStatusEffectComponent* StatusComp = TargetActor->FindComponentByClass<UStatusEffectComponent>())
	{
		StatusComp->ApplyStatus(StatusType, Duration, InstigatorActor);
		return true;
	}

	// 3. Architektura niszczalna (np. drewniana ściana)
	if (ADungeonStructureBase* Structure = Cast<ADungeonStructureBase>(TargetActor))
	{
		if (StatusType == EStatusEffectType::Burning && Structure->IsDestructible() && Structure->GetDamageableComponent())
		{
			Structure->GetDamageableComponent()->ApplyDamage(25.0f);
			return true;
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (UWorld* World = TargetActor->GetWorld())
	{
		DrawDebugPoint(World, HitLocation, 12.0f, FColor::Yellow, false, 2.0f);
		DrawDebugDirectionalArrow(World, HitLocation, HitLocation + HitNormal * 30.0f, 15.0f, FColor::Yellow, false, 2.0f, 0, 2.0f);
	}
#endif

	return false;
}

AElementalStatusZone* UElementalDeliveryLibrary::ApplySurfaceSplash(
	const UObject* WorldContextObject,
	const FHitResult& HitResult,
	float SplashRadius,
	EStatusEffectType StatusType,
	float Duration,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || !HitResult.bBlockingHit || StatusType == EStatusEffectType::None)
	{
		return nullptr;
	}

	AActor* HitActor = HitResult.GetActor();

	// 1. Jeśli trafiliśmy bezpośrednio w postać lub prop z komponentem statusów
	if (HitActor)
	{
		if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
		{
			StatusComp->ApplyStatus(StatusType, Duration, InstigatorActor);
		}
		else if (AElementalStatusZone* ExistingZone = Cast<AElementalStatusZone>(HitActor))
		{
			ExistingZone->ApplyElementalHit(StatusType, 0.0f, InstigatorActor);
			return ExistingZone;
		}
	}

	// 2. Spawnowanie płaskiej strefy rozbryzgu na powierzchni uderzenia (ściana lub podłoga)
	const FVector SurfaceNormal = HitResult.ImpactNormal.IsNearlyZero() ? FVector::UpVector : HitResult.ImpactNormal;
	const FVector SplashLocation = HitResult.ImpactPoint + SurfaceNormal * 2.0f;

	return SpawnStatusZone(
		WorldContextObject,
		SplashLocation,
		SplashRadius,
		StatusType,
		Duration,
		EStatusZoneShapeMode::SurfaceDisk,
		SurfaceNormal,
		InstigatorActor);
}

void UElementalDeliveryLibrary::ApplyRadialBurst(
	const UObject* WorldContextObject,
	const FVector& Origin,
	float Radius,
	EStatusEffectType StatusType,
	float Duration,
	AActor* InstigatorActor,
	float BaseDamage,
	float KnockbackForce)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f)
	{
		return;
	}

	// 1. Eksplozja fizyczna (Kinetic Force + obrażenia) z ekranowaniem Line-of-Sight
	if (BaseDamage > 0.0f || KnockbackForce > 0.0f)
	{
		UKineticForceLibrary::ApplyExplosion(WorldContextObject, Origin, Radius, BaseDamage, KnockbackForce, InstigatorActor, nullptr, false);
	}

	// 2. Aplikowanie statusu żywiołowego do obiektów w polu widzenia wybuchu (LoS)
	if (StatusType != EStatusEffectType::None)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadialBurstQuery), false, InstigatorActor);

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

			// Inna strefa statusu (np. wybuch ognia na plamie oleju)
			if (AElementalStatusZone* OtherZone = Cast<AElementalStatusZone>(HitActor))
			{
				OtherZone->ApplyElementalHit(StatusType, BaseDamage, InstigatorActor);
				ProcessedActors.Add(HitActor);
				continue;
			}

			// Line of Sight - fala wybuchu zatrzymuje się na ścianach
			FHitResult LoSHit;
			if (!UKineticForceLibrary::HasExplosionLineOfSight(World, Origin, HitActor, Overlap.GetComponent(), LoSHit, InstigatorActor))
			{
				continue;
			}

			ProcessedActors.Add(HitActor);

			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(StatusType, Duration, InstigatorActor);
			}

			if (StatusType == EStatusEffectType::Burning)
			{
				if (ADungeonStructureBase* Structure = Cast<ADungeonStructureBase>(HitActor))
				{
					if (Structure->IsDestructible() && Structure->GetDamageableComponent())
					{
						Structure->GetDamageableComponent()->ApplyDamage(BaseDamage);
					}
				}
			}
		}
	}
}

AElementalStatusZone* UElementalDeliveryLibrary::SpawnStatusZone(
	const UObject* WorldContextObject,
	const FVector& Location,
	float Radius,
	EStatusEffectType StatusType,
	float Duration,
	EStatusZoneShapeMode ShapeMode,
	const FVector& SurfaceNormal,
	AActor* InstigatorActor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || StatusType == EStatusEffectType::None)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Instigator = Cast<APawn>(InstigatorActor);

	AElementalStatusZone* Zone = World->SpawnActor<AElementalStatusZone>(
		AElementalStatusZone::StaticClass(),
		Location,
		FRotationMatrix::MakeFromX(SurfaceNormal).Rotator(),
		SpawnParams);

	if (Zone)
	{
		Zone->InitializeZone(StatusType, Radius, Duration, ShapeMode, SurfaceNormal, InstigatorActor);
	}

	return Zone;
}

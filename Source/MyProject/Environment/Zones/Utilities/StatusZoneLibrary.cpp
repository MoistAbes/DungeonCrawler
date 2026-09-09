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
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "Engine/Brush.h"

bool UStatusZoneLibrary::IsValidSurfaceTarget(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	// 1. Wykluczamy postacie oraz dynamiczne/interaktywne rekwizyty lochu (beczki, skrzynie)
	if (Actor->IsA<APawn>() || Actor->IsA<AInteractivePropBase>())
	{
		return false;
	}

	// 2. Akceptujemy oficjalne fundamenty i architekturę lochu (ściany, podłogi, sufity)
	if (Actor->IsA<ADungeonStructureBase>())
	{
		return true;
	}

	// 3. Akceptujemy geometrię poziomu (BSP Brushes map testowych i prototypowych)
	if (Actor->IsA<ABrush>())
	{
		return true;
	}

	return false;
}

ASurfaceSplashZone* UStatusZoneLibrary::ApplySurfaceSplash(
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

	// 1. Jeśli trafiliśmy bezpośrednio w postać lub strefę
	if (HitActor)
	{
		// Trafienie w istniejącą strefę
		if (AStatusZoneBase* ExistingZone = Cast<AStatusZoneBase>(HitActor))
		{
			ExistingZone->ApplyElementalHit(EffectConfig.AppliedStatus, 0.0f, InstigatorActor);
			return Cast<ASurfaceSplashZone>(ExistingZone);
		}

		if (EffectConfig.AppliedStatus != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(EffectConfig.AppliedStatus, Duration, InstigatorActor);
			}
		}

		// Opcja B: Jeśli trafiliśmy w postać / Pawn, szukamy posadzki pod jej stopami, aby rozlać plamę na podłodze
		if (HitActor->IsA<APawn>())
		{
			const FVector PawnLocation = HitActor->GetActorLocation();
			FHitResult FloorHit;
			FCollisionQueryParams FloorTraceParams(SCENE_QUERY_STAT(SurfaceSplashFloorTrace), false, HitActor);
			FloorTraceParams.AddIgnoredActor(HitActor);
			if (InstigatorActor)
			{
				FloorTraceParams.AddIgnoredActor(InstigatorActor);
			}

			const FVector TraceStart = PawnLocation;
			const FVector TraceEnd = PawnLocation - FVector(0.0f, 0.0f, 300.0f);

			if (World->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd, ECC_WorldStatic, FloorTraceParams))
			{
				return ApplySurfaceSplash(WorldContextObject, FloorHit, SplashRadius, SurfaceHeight, EffectConfig, Duration, InstigatorActor);
			}
			else
			{
				return nullptr;
			}
		}
	}

	// Strefa powierzchniowa może powstać WYŁĄCZNIE na fundamentach lochu lub geometrii poziomu
	if (!IsValidSurfaceTarget(HitActor))
	{
		return nullptr;
	}

	// 2. Wyliczenie pozycji i orientacji powłoki powierzchniowej
	const FVector SurfaceNormal = HitResult.ImpactNormal.IsNearlyZero() ? FVector::UpVector : HitResult.ImpactNormal.GetSafeNormal();
	const FVector SpawnLocation = HitResult.ImpactPoint + SurfaceNormal * 2.0f;
	const FRotator SpawnRotation = FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = InstigatorActor;
	SpawnParams.Instigator = Cast<APawn>(InstigatorActor);

	ASurfaceSplashZone* Zone = World->SpawnActor<ASurfaceSplashZone>(ASurfaceSplashZone::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
	if (!Zone)
	{
		return nullptr;
	}

	// 3. Przyczepienie strefy do trafionej ściany/podłogi (AttachToComponent)
	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		Zone->AttachToComponent(HitComponent, FAttachmentTransformRules::KeepWorldTransform);
	}

	Zone->InitializeSurfaceSplash(
		EffectConfig,
		SplashRadius,
		Duration,
		SurfaceHeight,
		SurfaceNormal,
		InstigatorActor);

	UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Spawned Surface Splash (Radius: %.1f cm, Height: %.1f cm) on %s"),
		SplashRadius, SurfaceHeight, *GetNameSafe(HitActor));

	return Zone;
}

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
			if (AStatusZoneBase* OtherZone = Cast<AStatusZoneBase>(HitActor))
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

	// 1. Jeśli uderzyliśmy w strefę (np. płonąca strzała w plamę oleju)
	if (AStatusZoneBase* Zone = Cast<AStatusZoneBase>(TargetActor))
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

	// 3. Architektura niszczalna (drewniane elementy pod wpływem ognia)
	if (ADungeonStructureBase* Structure = Cast<ADungeonStructureBase>(TargetActor))
	{
		if (StatusType == EStatusEffectType::Burning && Structure->IsDestructible() && Structure->GetDamageableComponent())
		{
			if (Structure->GetMaterialType_Implementation() == EPhysicalMaterialType::Wood)
			{
				Structure->GetDamageableComponent()->ApplyDamage(25.0f);
				return true;
			}
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


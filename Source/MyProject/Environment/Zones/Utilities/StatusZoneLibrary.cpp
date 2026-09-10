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

	// 2a. Zone Merging: Sprawdzamy, czy na tej samej powierzchni istnieje już strefa tego samego żywiołu
	if (EffectConfig.AppliedStatus != EStatusEffectType::None)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape OverlapSphere = FCollisionShape::MakeSphere(SplashRadius * 0.85f);
		FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(StatusZoneMergeQuery), false);

		if (World->OverlapMultiByChannel(Overlaps, HitResult.ImpactPoint, FQuat::Identity, ECC_WorldDynamic, OverlapSphere, OverlapParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (ASurfaceSplashZone* ExistingSplash = Cast<ASurfaceSplashZone>(Overlap.GetActor()))
				{
					if (ExistingSplash->GetStatusType() == EffectConfig.AppliedStatus)
					{
						// Ta sama płaszczyzna: zgodny wektor normalny oraz brak przesunięcia płaszczyzny
						if (ExistingSplash->IsCoplanarWith(HitResult.ImpactPoint, SurfaceNormal))
						{
							ExistingSplash->MergeWithZone(Duration, 1.20f, SplashRadius * 1.5f);
							UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Merged into existing Surface Splash (Radius: %.1f cm)"), ExistingSplash->GetRadius());
							return ExistingSplash;
						}
					}
				}
			}
		}
	}

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
						UE_LOG(LogDungeonElements, Log, TEXT("[StatusZoneLibrary] Merged into existing Volumetric Zone (Radius: %.1f cm)"), ExistingVolumetric->GetRadius());
						return ExistingVolumetric;
					}
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

void UStatusZoneLibrary::ApplyInstantBurst(
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

	// 1. Zunifikowany pojedynczy przebieg przestrzenny (Single-Pass Query)
	// Wykrywamy: postacie (Pawn), obiekty fizyczne (PhysicsBody), dynamiczne (WorldDynamic) oraz niszczalne struktury (WorldStatic)
	TArray<FOverlapResult> Overlaps;
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StatusZoneInstantBurst), false);
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

		// C. Aplikacja statusu żywiołowego
		if (EffectConfig.AppliedStatus != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(EffectConfig.AppliedStatus, Duration, InstigatorActor);
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


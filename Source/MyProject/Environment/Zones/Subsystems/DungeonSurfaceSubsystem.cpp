#include "DungeonSurfaceSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridProjectionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridPropagationUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Interfaces/SurfaceGridTargetInterface.h"
#include "MyProject/Logging/DungeonLogCategories.h"

UDungeonSurfaceSubsystem::UDungeonSurfaceSubsystem()
{
	CellSize = 50.0f;
	SubsystemTickInterval = 0.25f;
	bDrawDebugGrid = true;
}

bool UDungeonSurfaceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	return World->WorldType == EWorldType::Game
		|| World->WorldType == EWorldType::PIE
		|| World->WorldType == EWorldType::Editor;
}

void UDungeonSurfaceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_Client)
	{
		World->GetTimerManager().SetTimer(
			GridTickTimerHandle,
			this,
			&UDungeonSurfaceSubsystem::ProcessGridTick,
			SubsystemTickInterval,
			true);
	}

	UE_LOG(LogDungeonElements, Log, TEXT("[DungeonSurfaceSubsystem] Initialized with CellSize=%.1f, TickInterval=%.2fs"),
		CellSize, SubsystemTickInterval);
}

void UDungeonSurfaceSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GridTickTimerHandle);
	}

	RegisteredStatusComponents.Empty();
	Super::Deinitialize();

	UE_LOG(LogDungeonElements, Log, TEXT("[DungeonSurfaceSubsystem] Deinitialized."));
}

bool UDungeonSurfaceSubsystem::IsValidSurfaceTarget(const AActor* Actor)
{
	return SurfaceGridGeometryUtils::IsValidSurfaceTarget(Actor);
}

USceneComponent* UDungeonSurfaceSubsystem::GetDynamicActorTransformComponent(AActor* Actor)
{
	return FDynamicSurfaceGridManager::GetDynamicActorTransformComponent(Actor);
}

FTransform UDungeonSurfaceSubsystem::GetDynamicRigidTransform(const USceneComponent* Comp)
{
	return FDynamicSurfaceGridManager::GetDynamicRigidTransform(Comp);
}

int32 UDungeonSurfaceSubsystem::ApplyStatusFromHit(
	const FHitResult& HitResult,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !HitResult.bBlockingHit || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	AActor* HitActor = HitResult.GetActor();
	if (!IsValidSurfaceTarget(HitActor))
	{
		return 0;
	}

	const FVector SurfaceNormal = HitResult.ImpactNormal.IsNearlyZero() ? FVector::UpVector : HitResult.ImpactNormal.GetSafeNormal();

	// Jeśli cel to dynamiczny mechanizm (np. brama, winda), kierujemy do siatki lokalnej mesha
	if (HitActor && HitActor->Implements<USurfaceGridTargetInterface>() && ISurfaceGridTargetInterface::Execute_IsDynamicSurface(HitActor))
	{
		USceneComponent* TransformComp = GetDynamicActorTransformComponent(HitActor);
		if (TransformComp)
		{
			return ApplyStatusToDynamicSurface(
				HitActor,
				TransformComp,
				HitResult.ImpactPoint,
				SurfaceNormal,
				Radius,
				Status,
				Duration,
				Instigator,
				Tier);
		}
	}

	// Ścieżka statyczna (globalna siatka świata)
	return ApplyStatusToSurface(
		HitResult.ImpactPoint,
		SurfaceNormal,
		Radius,
		Status,
		Duration,
		Instigator,
		Tier);
}

int32 UDungeonSurfaceSubsystem::ApplyStatusToDynamicSurface(
	AActor* DynamicActor,
	USceneComponent* TransformComp,
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	if (!GetWorld() || !DynamicActor || !TransformComp || Radius <= 0.0f || Status == EStatusEffectType::None || Duration <= 0.0f)
	{
		return 0;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = GetWorld()->GetTimeSeconds();

	return DynamicGridManager.ApplyStatusToDynamicSurface(
		DynamicActor,
		TransformComp,
		HitLocation,
		HitNormal,
		Radius,
		Status,
		Duration,
		Instigator,
		Tier,
		SafeCellSize,
		CurrentTime,
		[this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* InInstigator)
		{
			OnSurfaceCellChanged.Broadcast(Coord, NewStatus, InInstigator);
		});
}

bool UDungeonSurfaceSubsystem::ApplyStatusToDynamicCell(
	AActor* DynamicActor,
	USceneComponent* TransformComp,
	const FSurfaceCellCoord& LocalCoord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial,
	uint8 Tier)
{
	if (!GetWorld() || !DynamicActor || IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = GetWorld()->GetTimeSeconds();

	return DynamicGridManager.ApplyStatusToDynamicCell(
		DynamicActor,
		TransformComp,
		LocalCoord,
		IncomingStatus,
		Duration,
		Instigator,
		ExplicitMaterial,
		Tier,
		SafeCellSize,
		CurrentTime,
		[this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* InInstigator)
		{
			OnSurfaceCellChanged.Broadcast(Coord, NewStatus, InInstigator);
		});
}

bool UDungeonSurfaceSubsystem::ApplyStatusToCell(
	const FSurfaceCellCoord& Coord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial,
	AActor* SurfaceActor,
	uint8 Tier,
	const FVector& SurfaceLocation)
{
	UWorld* World = GetWorld();
	if (!World || IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = World->GetTimeSeconds();

	EPhysicalMaterialType SurfaceMat = ExplicitMaterial;
	AActor* ResolvedSurfaceActor = SurfaceActor;

	if (!ResolvedSurfaceActor)
	{
		AActor* ProbedActor = nullptr;
		if (!GetSurfaceMaterialAtCoord(Coord, SurfaceMat, ProbedActor))
		{
			return false;
		}
		ResolvedSurfaceActor = ProbedActor;
	}

	// Jeśli cel to dynamiczny mechanizm (np. brama, winda), kierujemy do siatki lokalnej mesha
	if (ResolvedSurfaceActor && ResolvedSurfaceActor->Implements<USurfaceGridTargetInterface>()
		&& ISurfaceGridTargetInterface::Execute_IsDynamicSurface(ResolvedSurfaceActor))
	{
		USceneComponent* TransformComp = GetDynamicActorTransformComponent(ResolvedSurfaceActor);
		if (TransformComp)
		{
			const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
			const FVector WorldCellPos = Coord.ToWorldLocation(SafeCellSize);
			const FVector LocalPos = RigidTransform.InverseTransformPosition(WorldCellPos);
			const FVector WorldNormal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
			const FVector LocalNorm = RigidTransform.InverseTransformVector(WorldNormal);
			const FSurfaceCellCoord LocalCoord = FSurfaceCellCoord::FromWorldLocation(LocalPos, LocalNorm, SafeCellSize);

			return ApplyStatusToDynamicCell(ResolvedSurfaceActor, TransformComp, LocalCoord, IncomingStatus, Duration, Instigator, SurfaceMat, Tier);
		}
	}

	return StaticGridManager.ApplyStatusToCell(
		World,
		Coord,
		IncomingStatus,
		Duration,
		Instigator,
		SurfaceMat,
		ResolvedSurfaceActor,
		Tier,
		SafeCellSize,
		CurrentTime,
		[this](const FSurfaceCellCoord& InCoord, EStatusEffectType NewStatus, AActor* InInstigator)
		{
			OnSurfaceCellChanged.Broadcast(InCoord, NewStatus, InInstigator);
		},
		SurfaceLocation);
}

int32 UDungeonSurfaceSubsystem::ApplyStatusToSurface(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	UWorld* World = GetWorld();
	if (!World || Status == EStatusEffectType::None || Radius <= 0.0f || Duration <= 0.0f)
	{
		return 0;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	int32 AffectedCount = 0;

	SurfaceGridProjectionUtils::ProjectStatusToSurface(
		World,
		HitLocation,
		HitNormal,
		Radius,
		SafeCellSize,
		Instigator,
		[&](const FSurfaceCellCoord& Coord, EPhysicalMaterialType HitMat, AActor* SurfaceActor, const FVector& ImpactPoint)
		{
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator, HitMat, SurfaceActor, Tier, ImpactPoint))
			{
				AffectedCount++;
			}
		});

	if (AffectedCount > 0 && UElementalReactionRules::IsInstantConduction(Status))
	{
		FSurfaceGridPropagationUtils::PropagateConductionNetworks(
			World,
			StaticGridManager,
			DynamicGridManager,
			SafeCellSize,
			World->GetTimeSeconds(),
			[this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* InInstigator)
			{
				OnSurfaceCellChanged.Broadcast(Coord, NewStatus, InInstigator);
			});
	}

	return AffectedCount;
}

int32 UDungeonSurfaceSubsystem::ApplyStatusInArea(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	const FVector& BurstOrigin,
	TSet<FSurfaceCellCoord>& ProcessedCoords,
	AActor* Instigator,
	uint8 Tier)
{
	UWorld* World = GetWorld();
	if (!World || Status == EStatusEffectType::None || Radius <= 0.0f || Duration <= 0.0f)
	{
		return 0;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	int32 AffectedCount = 0;

	SurfaceGridProjectionUtils::ProjectStatusInArea(
		World,
		HitLocation,
		HitNormal,
		Radius,
		BurstOrigin,
		SafeCellSize,
		ProcessedCoords,
		Instigator,
		[&](const FSurfaceCellCoord& Coord, EPhysicalMaterialType HitMat, AActor* SurfaceActor, const FVector& ImpactPoint)
		{
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator, HitMat, SurfaceActor, Tier, ImpactPoint))
			{
				AffectedCount++;
			}
		});

	if (AffectedCount > 0 && UElementalReactionRules::IsInstantConduction(Status))
	{
		FSurfaceGridPropagationUtils::PropagateConductionNetworks(
			World,
			StaticGridManager,
			DynamicGridManager,
			SafeCellSize,
			World->GetTimeSeconds(),
			[this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* InInstigator)
			{
				OnSurfaceCellChanged.Broadcast(Coord, NewStatus, InInstigator);
			});
	}

	return AffectedCount;
}

int32 UDungeonSurfaceSubsystem::ClearCellsInBounds(const FBox& BoundingBox)
{
	if (!BoundingBox.IsValid)
	{
		return 0;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	auto BroadcastCellCleared = [this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* Instigator)
	{
		OnSurfaceCellChanged.Broadcast(Coord, NewStatus, Instigator);
	};

	int32 RemovedCount = StaticGridManager.ClearCellsInBounds(BoundingBox, BroadcastCellCleared);
	RemovedCount += DynamicGridManager.ClearCellsInBounds(BoundingBox, SafeCellSize);

	if (RemovedCount > 0)
	{
		UE_LOG(LogDungeonElements, Log, TEXT("[DungeonSurfaceSubsystem] Cleared %d cells in destroyed structure bounds"), RemovedCount);
	}

	return RemovedCount;
}

int32 UDungeonSurfaceSubsystem::ApplyElementalBurst(
	const FVector& Origin,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	if (!GetWorld() || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	// UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyElementalBurst START -> Origin: %s, Radius: %.1f, Status: %s (Tier: %d)"),
	// 	*Origin.ToString(), Radius, *UEnum::GetValueAsString(Status), Tier);

	int32 AffectedCount = 0;
	TSet<FSurfaceCellCoord> ProcessedCoords;

	// 1. Bezpośrednia ewaluacja istniejących aktywnych komórek w sferze wybuchu z Line-of-Sight
	TArray<FSurfaceCellCoord> CellsInRadius;
	SurfaceGridProjectionUtils::FilterCellsInBurstRadius(
		GetWorld(),
		Origin,
		Radius,
		CellSize,
		StaticGridManager.GetActiveCells(),
		Instigator,
		CellsInRadius);

	for (const FSurfaceCellCoord& Coord : CellsInRadius)
	{
		if (!StaticGridManager.GetActiveCells().Contains(Coord))
		{
			continue;
		}

		ProcessedCoords.Add(Coord);
		if (ApplyStatusToCell(Coord, Status, Duration, Instigator, EPhysicalMaterialType::Stone, nullptr, Tier))
		{
			AffectedCount++;
		}
	}

	// 1b. Bezpośrednia ewaluacja istniejących komórek dynamicznych w sferze wybuchu
	struct FPendingDynBurstCell
	{
		TWeakObjectPtr<AActor> DynActor;
		TWeakObjectPtr<USceneComponent> TransformComp;
		FSurfaceCellCoord LocalCoord;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
	};
	TArray<FPendingDynBurstCell> PendingDynBurstCells;

	for (const auto& GridPair : DynamicGridManager.GetGrids())
	{
		const FDynamicActorSurfaceGrid& DynGrid = GridPair.Value;
		AActor* DynActor = DynGrid.OwnerActor.Get();
		USceneComponent* TransformComp = DynGrid.TransformComponent.Get();
		if (!DynActor || !TransformComp || DynGrid.LocalCells.IsEmpty())
		{
			continue;
		}

		const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
		for (const auto& CellPair : DynGrid.LocalCells)
		{
			const FSurfaceCellCoord& LocalCoord = CellPair.Key;
			const FVector LocalCenter = LocalCoord.ToWorldLocation(CellSize);
			const FVector WorldCenter = RigidTransform.TransformPosition(LocalCenter);

			if (FVector::DistSquared(Origin, WorldCenter) > FMath::Square(Radius))
			{
				continue;
			}

			FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(DynamicCellBurstLoS), false, Instigator);
			LoSParams.AddIgnoredActor(DynActor);
			FHitResult LoSHit;
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(LoSHit, Origin, WorldCenter, ECC_Visibility, LoSParams);
			if (!bBlocked || LoSHit.GetActor() == DynActor)
			{
				PendingDynBurstCells.Add({ DynActor, TransformComp, LocalCoord, CellPair.Value.SurfaceMaterial });
			}
		}
	}

	for (const FPendingDynBurstCell& PendingCell : PendingDynBurstCells)
	{
		if (PendingCell.DynActor.IsValid() && PendingCell.TransformComp.IsValid())
		{
			if (ApplyStatusToDynamicCell(PendingCell.DynActor.Get(), PendingCell.TransformComp.Get(), PendingCell.LocalCoord, Status, Duration, Instigator, PendingCell.Material, Tier))
			{
				AffectedCount++;
			}
		}
	}

	// 2. Projekcja wybuchu na otaczające powierzchnie lochu
	SurfaceGridProjectionUtils::ScanBurstSurfaces(
		GetWorld(),
		Origin,
		Radius,
		CellSize,
		Instigator,
		[&](const FHitResult& SurfaceHit, float SplashRadius)
		{
			AffectedCount += ApplyStatusInArea(
				SurfaceHit.ImpactPoint,
				SurfaceHit.ImpactNormal,
				SplashRadius,
				Status,
				Duration,
				Origin,
				ProcessedCoords,
				Instigator,
				Tier);
		});

	if (AffectedCount > 0 && UElementalReactionRules::IsInstantConduction(Status))
	{
		const float SafeCellSize = FMath::Max(10.0f, CellSize);
		FSurfaceGridPropagationUtils::PropagateConductionNetworks(
			GetWorld(),
			StaticGridManager,
			DynamicGridManager,
			SafeCellSize,
			GetWorld()->GetTimeSeconds(),
			[this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* InInstigator)
			{
				OnSurfaceCellChanged.Broadcast(Coord, NewStatus, InInstigator);
			});
	}

	// UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyElementalBurst FINISH -> AffectedCount: %d"), AffectedCount);
	return AffectedCount;
}

void UDungeonSurfaceSubsystem::RegisterStatusComponent(UStatusEffectComponent* Comp)
{
	if (Comp && !RegisteredStatusComponents.Contains(Comp))
	{
		RegisteredStatusComponents.Add(Comp);
	}
}

void UDungeonSurfaceSubsystem::UnregisterStatusComponent(UStatusEffectComponent* Comp)
{
	RegisteredStatusComponents.Remove(Comp);
}

bool UDungeonSurfaceSubsystem::GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial) const
{
	AActor* DummyActor = nullptr;
	return GetSurfaceMaterialAtCoord(Coord, OutMaterial, DummyActor);
}

bool UDungeonSurfaceSubsystem::GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial, AActor*& OutSurfaceActor) const
{
	return FStaticSurfaceGridManager::GetSurfaceMaterialAtCoord(GetWorld(), FMath::Max(10.0f, CellSize), Coord, OutMaterial, OutSurfaceActor);
}

void UDungeonSurfaceSubsystem::ProcessGridTick()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	auto BroadcastCellChanged = [this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* Instigator)
	{
		OnSurfaceCellChanged.Broadcast(Coord, NewStatus, Instigator);
	};

	// 1. Wygaszanie przeterminowanych statusów w komórkach obu siatek
	StaticGridManager.ExpireCells(CurrentTime, BroadcastCellChanged);
	DynamicGridManager.ExpireCells(CurrentTime, BroadcastCellChanged);

	// 2. Data-driven automat komórkowy (dwukierunkowa propagacja żywiołów)
	FSurfaceGridPropagationUtils::PropagateElementalSpreads(
		World,
		StaticGridManager,
		DynamicGridManager,
		SafeCellSize,
		CurrentTime,
		BroadcastCellChanged);

	// 3. Rozprzestrzenianie stałego ognia (np. drewno) oraz niszczenie fundamentów lochu
	StaticGridManager.ProcessSolidFuelCombustion(World, SafeCellSize, CurrentTime, BroadcastCellChanged);
	StaticGridManager.ProcessSurfaceStructuralDamage(World, DynamicGridManager, SubsystemTickInterval);

	// 4. Interakcje z zarejestrowanymi postaciami
	ProcessActorInteractions(CurrentTime);

	// 5. Rysowanie debugowe
	if (bDrawDebugGrid)
	{
		DrawDebugVisuals();
	}
}

void UDungeonSurfaceSubsystem::ProcessActorInteractions(float CurrentTime)
{
	UWorld* World = GetWorld();
	if (!World || RegisteredStatusComponents.IsEmpty())
	{
		return;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const TArray<TWeakObjectPtr<UStatusEffectComponent>> ComponentsToProcess = RegisteredStatusComponents;

	for (const TWeakObjectPtr<UStatusEffectComponent>& WeakComp : ComponentsToProcess)
	{
		UStatusEffectComponent* StatusComp = WeakComp.Get();
		if (!StatusComp || !IsValid(StatusComp))
		{
			continue;
		}

		AActor* OwnerActor = StatusComp->GetOwner();
		if (!OwnerActor || OwnerActor->IsActorBeingDestroyed())
		{
			continue;
		}

		StaticGridManager.ProcessActorInteraction(
			OwnerActor,
			StatusComp,
			SafeCellSize,
			CurrentTime,
			[this](const FSurfaceCellCoord& Coord, EStatusEffectType Status, float Duration, AActor* Instigator)
			{
				return ApplyStatusToCell(Coord, Status, Duration, Instigator);
			});

		DynamicGridManager.ProcessActorInteractions(OwnerActor, StatusComp, SafeCellSize);
	}

	// Czyszczenie martwych wskaźników z rejestru
	for (int32 Idx = RegisteredStatusComponents.Num() - 1; Idx >= 0; --Idx)
	{
		if (!RegisteredStatusComponents[Idx].IsValid())
		{
			RegisteredStatusComponents.RemoveAt(Idx);
		}
	}
}

void UDungeonSurfaceSubsystem::DrawDebugVisuals() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDrawDebugGrid || !GetWorld())
	{
		return;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	StaticGridManager.DrawDebug(GetWorld(), SafeCellSize);
	DynamicGridManager.DrawDebug(GetWorld(), SafeCellSize);
#endif
}

#include "DungeonSurfaceSubsystem.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridProjectionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridPropagationUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
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
	if (SurfaceGridGeometryUtils::IsDynamicSurfaceTarget(HitActor))
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

	const bool bApplied = DynamicGridManager.ApplyStatusToDynamicCell(
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

	if (bApplied)
	{
		bConductionNetworkDirty = true;
	}

	return bApplied;
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
	if (SurfaceGridGeometryUtils::IsDynamicSurfaceTarget(ResolvedSurfaceActor))
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

	const bool bApplied = StaticGridManager.ApplyStatusToCell(
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

	if (bApplied)
	{
		bConductionNetworkDirty = true;
	}

	return bApplied;
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

	PropagateConductionIfApplicable(Status, AffectedCount);

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
	uint8 Tier,
	int32* OutNewlyCreatedCount)
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
			const bool bIsNew = !StaticGridManager.GetActiveCells().Contains(Coord);
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator, HitMat, SurfaceActor, Tier, ImpactPoint))
			{
				AffectedCount++;
				if (bIsNew && OutNewlyCreatedCount)
				{
					(*OutNewlyCreatedCount)++;
				}
			}
		});

	// Jeśli wywołane samodzielnie (poza ApplyElementalBurst), odpalamy propagację
	if (!OutNewlyCreatedCount)
	{
		PropagateConductionIfApplicable(Status, AffectedCount);
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

	int32 RemovedCount = StaticGridManager.ClearCellsInBounds(BoundingBox, SafeCellSize, BroadcastCellCleared);
	RemovedCount += DynamicGridManager.ClearCellsInBounds(BoundingBox, SafeCellSize, BroadcastCellCleared);

	if (RemovedCount > 0)
	{
		bConductionNetworkDirty = true;
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
	UWorld* World = GetWorld();
	if (!World || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	int32 AffectedCount = 0;
	int32 NewlyCreatedCount = 0;
	TSet<FSurfaceCellCoord> ProcessedCoords;
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = World->GetTimeSeconds();

	// 1. Bezpośrednia ewaluacja istniejących aktywnych komórek w sferze wybuchu z Line-of-Sight
	TArray<FSurfaceCellCoord> CellsInRadius;
	SurfaceGridProjectionUtils::FilterCellsInBurstRadius(
		World,
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
	AffectedCount += DynamicGridManager.ApplyElementalBurst(
		World,
		Origin,
		Radius,
		Status,
		Duration,
		Instigator,
		Tier,
		SafeCellSize,
		CurrentTime,
		[this](const FSurfaceCellCoord& CellCoord, EStatusEffectType NewStatus, AActor* InInstigator)
		{
			OnSurfaceCellChanged.Broadcast(CellCoord, NewStatus, InInstigator);
		});

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
				Tier,
				&NewlyCreatedCount);
		});

	// Jeśli wybuch zaaplikował wyłącznie nośnik płynny (np. Wet) i nie powstała ani jedna nowa komórka w świecie
	// (wszystkie istniały już wcześniej i zostały tylko odświeżone czasowo), sieć nie uległa rozszerzeniu i pomijamy BFS!
	const bool bIsCarrierOnly = UElementalReactionRules::IsLiquidStatus(Status) && !UElementalReactionRules::IsInstantConduction(Status);
	if (!bIsCarrierOnly || NewlyCreatedCount > 0)
	{
		PropagateConductionIfApplicable(Status, AffectedCount);
	}

	return AffectedCount;
}

int32 UDungeonSurfaceSubsystem::ApplyContinuousZoneToCells(
	const FVector& Origin,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	UWorld* World = GetWorld();
	if (!World || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	int32 AffectedCount = 0;
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = World->GetTimeSeconds();
	const float RadiusSq = FMath::Square(Radius);

	// Bezpieczny próg bufora wyprzedzenia: jeśli komórka już posiada ten status i ma bezpieczny zapas czasu (> 1.5s),
	// to w tym ticku strefa ją ignoruje (zero trace'ów LoS, zero zapytań fizyki, zero broadcastów eventów, zero BFS).
	const float MinRemainingToSkip = FMath::Max(1.5f, Duration * 0.4f);

	FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(ZoneContinuousCellLoS), false, Instigator);
	if (Instigator)
	{
		LoSParams.AddIgnoredActor(Instigator);
	}

	struct FPendingZoneStaticCell
	{
		FSurfaceCellCoord Coord;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
		TWeakObjectPtr<AActor> SurfaceActor = nullptr;
		FVector SurfaceLocation = FVector::ZeroVector;
	};
	TArray<FPendingZoneStaticCell> PendingCells;

	// KROK 1: Bezpieczne zebranie kandydatów w trybie Read-Only (brak modyfikacji TMap w pętli)
	for (const auto& Pair : StaticGridManager.GetActiveCells())
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FSurfaceCellData& Data = Pair.Value;

		if (Data.IsEmpty())
		{
			continue;
		}

		const FVector CellWorldPos = Coord.ToWorldLocation(SafeCellSize);
		if (FVector::DistSquared(Origin, CellWorldPos) > RadiusSq)
		{
			continue;
		}

		// KROK A: Jeśli komórka ma już ten status i nie wygasa w najbliższym czasie, pomijamy!
		const FSurfaceCellStatusEntry* ExistingEntry = Data.FindStatus(Status);
		if (ExistingEntry && (ExistingEntry->IsPermanent() || ExistingEntry->GetRemainingDuration(CurrentTime) > MinRemainingToSkip))
		{
			continue;
		}

		// KROK B: Test Line-of-Sight wykonujemy wyłącznie dla komórek realnie wymagających nałożenia/odświeżenia
		const FVector CellNormal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
		const FVector CellSurfacePos = CellWorldPos + CellNormal * (SafeCellSize * 0.45f);

		if (!SurfaceGridGeometryUtils::HasDirectBurstLineOfSight(World, Origin, CellSurfacePos, CellNormal, Data.SurfaceActor.Get(), LoSParams))
		{
			continue;
		}

		PendingCells.Add({ Coord, Data.SurfaceMaterial, Data.SurfaceActor.Get(), Data.SurfaceLocation });
	}

	// KROK 2: Aplikacja statusu po lokalnym TArray (bezpieczne przed reallokacją TMapy)
	for (const FPendingZoneStaticCell& Pending : PendingCells)
	{
		if (ApplyStatusToCell(Pending.Coord, Status, Duration, Instigator, Pending.Material, Pending.SurfaceActor.Get(), Tier, Pending.SurfaceLocation))
		{
			AffectedCount++;
		}
	}

	// 2. Bezpośrednia ewaluacja istniejących komórek dynamicznych w sferze strefy
	if (DynamicGridManager.GetGrids().Num() > 0)
	{
		AffectedCount += DynamicGridManager.ApplyElementalBurst(
			World,
			Origin,
			Radius,
			Status,
			Duration,
			Instigator,
			Tier,
			SafeCellSize,
			CurrentTime,
			[this](const FSurfaceCellCoord& CellCoord, EStatusEffectType NewStatus, AActor* InInstigator)
			{
				OnSurfaceCellChanged.Broadcast(CellCoord, NewStatus, InInstigator);
			});
	}

	if (AffectedCount > 0)
	{
		PropagateConductionIfApplicable(Status, AffectedCount);
	}

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

	bool bCellsChangedInTick = false;
	auto BroadcastCellChanged = [this, &bCellsChangedInTick](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* Instigator)
	{
		bCellsChangedInTick = true;
		OnSurfaceCellChanged.Broadcast(Coord, NewStatus, Instigator);
	};

	// 1. Wygaszanie przeterminowanych statusów w komórkach obu siatek
	StaticGridManager.ExpireCells(CurrentTime, BroadcastCellChanged);
	DynamicGridManager.ExpireCells(CurrentTime, BroadcastCellChanged);

	if (bCellsChangedInTick)
	{
		bConductionNetworkDirty = true;
	}

	// 2. Data-driven automat komórkowy (dwukierunkowa propagacja żywiołów)
	FSurfaceGridPropagationUtils::PropagateElementalSpreads(
		World,
		StaticGridManager,
		DynamicGridManager,
		SafeCellSize,
		CurrentTime,
		bConductionNetworkDirty,
		BroadcastCellChanged);

	// Po wykonaniu propagacji sieć osiąga stan stabilny
	bConductionNetworkDirty = false;

	// 3. Rozprzestrzenianie stałego ognia (np. drewno) oraz niszczenie fundamentów lochu
	StaticGridManager.ProcessSolidFuelCombustion(World, SafeCellSize, CurrentTime, BroadcastCellChanged);
	ProcessSurfaceStructuralDamage(SubsystemTickInterval);

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

		DynamicGridManager.ProcessActorInteractions(
			OwnerActor,
			StatusComp,
			SafeCellSize,
			CurrentTime,
			[this](const FSurfaceCellCoord& Coord, EStatusEffectType NewStatus, AActor* InInstigator)
			{
				OnSurfaceCellChanged.Broadcast(Coord, NewStatus, InInstigator);
			});
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

void UDungeonSurfaceSubsystem::ProcessSurfaceStructuralDamage(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	struct FDamageAggregate
	{
		float TotalDPS = 0.0f;
		TWeakObjectPtr<AActor> LastInstigator = nullptr;
	};

	TMap<TPair<TWeakObjectPtr<AActor>, EDamageType>, FDamageAggregate> AggregatedDamage;

	auto AggregateCellDamage = [&](const FSurfaceCellData& Cell)
	{
		if (Cell.IsEmpty() || !Cell.SurfaceActor.IsValid())
		{
			return;
		}

		for (const FSurfaceCellStatusEntry& StatusEntry : Cell.ActiveStatuses)
		{
			const FStatusEffectConfig& Config = UElementalReactionRules::GetEffectConfig(StatusEntry.Status);
			if (Config.bIsDoTType)
			{
				const EDamageType DmgType = PhysicalMaterialUtils::StatusToDamageType(StatusEntry.Status);
				const float BaseDPS = Config.GetDamagePerSecond(StatusEntry.Tier);
				if (BaseDPS > 0.0f)
				{
					const TPair<TWeakObjectPtr<AActor>, EDamageType> Key(Cell.SurfaceActor, DmgType);
					FDamageAggregate& Agg = AggregatedDamage.FindOrAdd(Key);
					Agg.TotalDPS += BaseDPS;
					if (StatusEntry.Instigator.IsValid())
					{
						Agg.LastInstigator = StatusEntry.Instigator;
					}
				}
			}
		}
	};

	// 1. Agregacja ze statycznych komórek
	for (const auto& CellPair : StaticGridManager.GetActiveCells())
	{
		AggregateCellDamage(CellPair.Value);
	}

	// 2. Agregacja z dynamicznych komórek
	for (const auto& GridPair : DynamicGridManager.GetGrids())
	{
		for (const auto& CellPair : GridPair.Value.LocalCells)
		{
			AggregateCellDamage(CellPair.Value);
		}
	}

	// 3. Aplikujemy zagregowane pakiety obrażeń do UDamageableComponent fundamentów
	for (const auto& AggPair : AggregatedDamage)
	{
		AActor* StructureActor = AggPair.Key.Key.Get();
		if (!StructureActor || !IsValid(StructureActor) || StructureActor->IsActorBeingDestroyed())
		{
			continue;
		}

		const EDamageType DmgType = AggPair.Key.Value;
		const FDamageAggregate& Agg = AggPair.Value;

		if (Agg.TotalDPS <= 0.0f)
		{
			continue;
		}

		UDamageableComponent* DmgComp = StructureActor->FindComponentByClass<UDamageableComponent>();
		if (!DmgComp || DmgComp->IsDestroyed() || DmgComp->IsInvulnerable())
		{
			continue;
		}

		const float ClampedDPS = FMath::Min(Agg.TotalDPS, MaxStructuralDPS);
		const float DamageAmount = ClampedDPS * DeltaTime;

		// UDamageableComponent automatycznie uwzględnia TotalResistance (np. 100% dla kamienia, 0% dla drewna na ogień)
		DmgComp->ApplyDamage(DamageAmount, DmgType, Agg.LastInstigator.Get());
	}
}

void UDungeonSurfaceSubsystem::PropagateConductionIfApplicable(EStatusEffectType Status, int32 AffectedCount)
{
	if (AffectedCount <= 0)
	{
		return;
	}

	// Propagujemy przewodzenie jeśli nałożono status przewodzący (np. prąd)
	// LUB nałożono płynny nośnik przewodzenia (np. wodę), który może połączyć się z istniejącym prądem!
	const bool bIsConductor = UElementalReactionRules::IsInstantConduction(Status);
	const bool bIsCarrier = UElementalReactionRules::IsLiquidStatus(Status);

	if (!bIsConductor && !bIsCarrier)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
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

	// Po wykonaniu propagacji sieć osiąga stan stabilny
	bConductionNetworkDirty = false;
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

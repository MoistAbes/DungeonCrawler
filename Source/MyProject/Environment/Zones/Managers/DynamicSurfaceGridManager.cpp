#include "DynamicSurfaceGridManager.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceCellTransitionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceActorInteractionUtils.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Interfaces/SurfaceGridTargetInterface.h"
#include "MyProject/Logging/DungeonLogCategories.h"

USceneComponent* FDynamicSurfaceGridManager::GetDynamicActorTransformComponent(AActor* Actor)
{
	if (!Actor || !Actor->Implements<USurfaceGridTargetInterface>())
	{
		return nullptr;
	}
	return ISurfaceGridTargetInterface::Execute_GetSurfaceTransformComponent(Actor);
}

FTransform FDynamicSurfaceGridManager::GetDynamicRigidTransform(const USceneComponent* Comp)
{
	if (!Comp)
	{
		return FTransform::Identity;
	}
	return FTransform(Comp->GetComponentRotation(), Comp->GetComponentLocation(), FVector::OneVector);
}

FSurfaceCellCoord FDynamicSurfaceGridManager::WorldToLocalCoord(
	const USceneComponent* Comp,
	const FVector& WorldLocation,
	const FVector& WorldNormal,
	float SafeCellSize)
{
	const FTransform RigidTransform = GetDynamicRigidTransform(Comp);
	const FVector LocalPos = RigidTransform.InverseTransformPosition(WorldLocation);
	const FVector LocalNorm = RigidTransform.InverseTransformVector(WorldNormal).GetSafeNormal();
	return FSurfaceCellCoord::FromWorldLocation(LocalPos, LocalNorm, SafeCellSize);
}

FVector FDynamicSurfaceGridManager::LocalToWorldLocation(
	const USceneComponent* Comp,
	const FSurfaceCellCoord& LocalCoord,
	float SafeCellSize)
{
	const FTransform RigidTransform = GetDynamicRigidTransform(Comp);
	return RigidTransform.TransformPosition(LocalCoord.ToWorldLocation(SafeCellSize));
}

FVector FDynamicSurfaceGridManager::LocalToWorldSurfaceContact(
	const USceneComponent* Comp,
	const FSurfaceCellCoord& LocalCoord,
	float SafeCellSize)
{
	const FTransform RigidTransform = GetDynamicRigidTransform(Comp);
	const FVector LocalContact = LocalCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(LocalCoord.Face) * (SafeCellSize * SurfaceGridConstants::NormalOffsetRatio);
	return RigidTransform.TransformPosition(LocalContact);
}

int32 FDynamicSurfaceGridManager::ApplyStatusToDynamicSurface(
	AActor* DynamicActor,
	USceneComponent* TransformComp,
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!DynamicActor || !TransformComp || Radius <= 0.0f || Status == EStatusEffectType::None || Duration <= 0.0f)
	{
		return 0;
	}

	const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
	const FVector LocalHitLocation = RigidTransform.InverseTransformPosition(HitLocation);
	const FVector LocalHitNormal = RigidTransform.InverseTransformVector(HitNormal).GetSafeNormal();

	const ESurfaceFaceDirection LocalFaceDir = SurfaceGridUtils::NormalToFaceDirection(LocalHitNormal);
	const FVector LocalNormal = SurfaceGridUtils::FaceDirectionToNormal(LocalFaceDir);

	FVector LocalTangentU;
	FVector LocalTangentV;
	SurfaceGridGeometryUtils::GetFaceTangents(LocalFaceDir, LocalTangentU, LocalTangentV);

	const int32 StepRadius = (Radius <= SafeCellSize * 0.5f) ? 0 : FMath::CeilToInt(Radius / SafeCellSize);
	const float RadiusSq = FMath::Square(Radius);

	const EPhysicalMaterialType HitMat = SurfaceGridGeometryUtils::GetMaterialFromActor(DynamicActor);
	const FBox CompWorldBox = TransformComp->Bounds.GetBox().ExpandBy(SafeCellSize * 0.6f);

	int32 AffectedCount = 0;

	for (int32 du = -StepRadius; du <= StepRadius; ++du)
	{
		for (int32 dv = -StepRadius; dv <= StepRadius; ++dv)
		{
			const FVector LocalOffset = (static_cast<float>(du) * LocalTangentU + static_cast<float>(dv) * LocalTangentV) * SafeCellSize;
			if (LocalOffset.SizeSquared() > RadiusSq)
			{
				continue;
			}

			const FVector LocalSamplePoint = LocalHitLocation + LocalOffset;
			const FVector WorldSamplePoint = RigidTransform.TransformPosition(LocalSamplePoint);

			// Test obecności powierzchni: upewniamy się, że próbka leży w obrębie bryły kolizyjnej komponentu
			if (!CompWorldBox.IsInsideOrOn(WorldSamplePoint))
			{
				continue;
			}

			const FSurfaceCellCoord LocalCoord = FSurfaceCellCoord::FromWorldLocation(LocalSamplePoint, LocalNormal, SafeCellSize);

			if (ApplyStatusToDynamicCell(DynamicActor, TransformComp, LocalCoord, Status, Duration, Instigator, HitMat, Tier, SafeCellSize, CurrentTime, OnCellChanged))
			{
				AffectedCount++;
			}
		}
	}

	return AffectedCount;
}

int32 FDynamicSurfaceGridManager::ApplyElementalBurst(
	UWorld* World,
	const FVector& Origin,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged,
	int32* OutNewlyAddedCount)
{
	if (!World || Status == EStatusEffectType::None || Duration < 0.0f)
	{
		return 0;
	}

	struct FPendingDynBurstCell
	{
		TWeakObjectPtr<AActor> DynActor;
		TWeakObjectPtr<USceneComponent> TransformComp;
		FSurfaceCellCoord LocalCoord;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
		bool bWasAlreadyPresent = false;
	};
	TArray<FPendingDynBurstCell> PendingDynBurstCells;

	const float RadiusSq = FMath::Square(Radius);
	const float MinRemainingToSkip = SurfaceGridConstants::ContinuousZoneSkipThreshold;

	for (const auto& GridPair : DynamicSurfaceGrids)
	{
		const FDynamicActorSurfaceGrid& DynGrid = GridPair.Value;
		AActor* DynActor = DynGrid.OwnerActor.Get();
		USceneComponent* TransformComp = DynGrid.TransformComponent.Get();
		if (!DynActor || !TransformComp || DynGrid.LocalCells.IsEmpty())
		{
			continue;
		}

		// Broad-phase: wczesne odrzucenie siatek dynamicznych leżących całkowicie poza sferą wybuchu/strefy
		const float DistSqToComp = TransformComp->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin);
		if (DistSqToComp > RadiusSq)
		{
			continue;
		}

		const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
		for (const auto& CellPair : DynGrid.LocalCells)
		{
			const FSurfaceCellCoord& LocalCoord = CellPair.Key;
			const FVector LocalCenter = LocalCoord.ToWorldLocation(SafeCellSize);
			const FVector WorldCenter = RigidTransform.TransformPosition(LocalCenter);

			if (FVector::DistSquared(Origin, WorldCenter) > RadiusSq)
			{
				continue;
			}

			// KROK A: Jeśli komórka dynamiczna ma już ten status w co najmniej tym samym Tierze i ma wystarczający zapas czasu (> 1.0s), pomijamy!
			const FSurfaceCellStatusEntry* ExistingEntry = CellPair.Value.FindStatus(Status);
			if (ExistingEntry && ExistingEntry->Tier >= Tier)
			{
				if (ExistingEntry->IsPermanent() || ExistingEntry->GetRemainingDuration(CurrentTime) > MinRemainingToSkip)
				{
					continue;
				}
			}

			// KROK B: Odrzucenie komórek dynamicznych, które fizycznie nie mogą przyjąć tego statusu ani nie wejdą w reakcję żywiołową
			if (!UElementalReactionRules::CanApplyStatusToTarget(CellPair.Value.SurfaceMaterial, Status, CellPair.Value.GetStatusTypes()))
			{
				continue;
			}

			FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(DynamicCellBurstLoS), false, Instigator);
			LoSParams.AddIgnoredActor(DynActor);
			FHitResult LoSHit;
			const bool bBlocked = World->LineTraceSingleByChannel(LoSHit, Origin, WorldCenter, ECC_Visibility, LoSParams);
			if (!bBlocked || LoSHit.GetActor() == DynActor)
			{
				PendingDynBurstCells.Add({ DynActor, TransformComp, LocalCoord, CellPair.Value.SurfaceMaterial, (ExistingEntry != nullptr) });
			}
		}
	}

	int32 AffectedCount = 0;
	int32 NewlyAdded = 0;
	for (const FPendingDynBurstCell& PendingCell : PendingDynBurstCells)
	{
		if (PendingCell.DynActor.IsValid() && PendingCell.TransformComp.IsValid())
		{
			if (ApplyStatusToDynamicCell(PendingCell.DynActor.Get(), PendingCell.TransformComp.Get(), PendingCell.LocalCoord, Status, Duration, Instigator, PendingCell.Material, Tier, SafeCellSize, CurrentTime, OnCellChanged))
			{
				AffectedCount++;
				if (!PendingCell.bWasAlreadyPresent)
				{
					NewlyAdded++;
				}
			}
		}
	}

	if (OutNewlyAddedCount)
	{
		*OutNewlyAddedCount = NewlyAdded;
	}

	return AffectedCount;
}

bool FDynamicSurfaceGridManager::ApplyStatusToDynamicCell(
	AActor* DynamicActor,
	USceneComponent* TransformComp,
	const FSurfaceCellCoord& LocalCoord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial,
	uint8 Tier,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!DynamicActor || IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	FDynamicActorSurfaceGrid& Grid = DynamicSurfaceGrids.FindOrAdd(DynamicActor);
	Grid.OwnerActor = DynamicActor;
	Grid.TransformComponent = TransformComp;
	if (Grid.LastTransform.Equals(FTransform::Identity))
	{
		Grid.LastTransform = GetDynamicRigidTransform(TransformComp);
	}

	const bool bModified = USurfaceCellTransitionUtils::ApplyStatusToCellInMap(
		Grid.LocalCells,
		LocalCoord,
		IncomingStatus,
		Duration,
		Instigator,
		ExplicitMaterial,
		DynamicActor,
		Tier,
		CurrentTime,
		OnCellChanged);

	if (Grid.LocalCells.IsEmpty())
	{
		DynamicSurfaceGrids.Remove(DynamicActor);
	}

	return bModified;
}

int32 FDynamicSurfaceGridManager::ClearCellsInBounds(
	const FBox& BoundingBox,
	float SafeCellSize,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	int32 RemovedCount = 0;
	const FBox ExpandedBox = BoundingBox.ExpandBy(SafeCellSize * 0.5f);

	for (auto GridIt = DynamicSurfaceGrids.CreateIterator(); GridIt; ++GridIt)
	{
		AActor* DynActor = GridIt.Key().Get();
		USceneComponent* TransformComp = GridIt.Value().TransformComponent.Get();
		if (!DynActor || !TransformComp)
		{
			GridIt.RemoveCurrent();
			continue;
		}

		const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
		for (auto CellIt = GridIt.Value().LocalCells.CreateIterator(); CellIt; ++CellIt)
		{
			const FVector WorldCenter = RigidTransform.TransformPosition(CellIt.Key().ToWorldLocation(SafeCellSize));
			if (ExpandedBox.IsInsideOrOn(WorldCenter))
			{
				OnCellChanged(CellIt.Key(), EStatusEffectType::None, nullptr);
				CellIt.RemoveCurrent();
				RemovedCount++;
			}
		}

		if (GridIt.Value().LocalCells.IsEmpty())
		{
			GridIt.RemoveCurrent();
		}
	}

	return RemovedCount;
}

void FDynamicSurfaceGridManager::ExpireCells(
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	for (auto GridIt = DynamicSurfaceGrids.CreateIterator(); GridIt; ++GridIt)
	{
		if (!GridIt.Key().IsValid() || !GridIt.Value().TransformComponent.IsValid())
		{
			GridIt.RemoveCurrent();
			continue;
		}

		USurfaceCellTransitionUtils::ExpireCellsInMap(GridIt.Value().LocalCells, CurrentTime, OnCellChanged);

		if (GridIt.Value().LocalCells.IsEmpty())
		{
			GridIt.RemoveCurrent();
		}
	}
}

void FDynamicSurfaceGridManager::ProcessActorInteractions(
	AActor* Actor,
	UStatusEffectComponent* StatusComp,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!Actor || Actor->IsActorBeingDestroyed() || !StatusComp || DynamicSurfaceGrids.IsEmpty())
	{
		return;
	}

	FBox ActorBox;
	if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		ActorBox = RootPrim->Bounds.GetBox();
	}
	else
	{
		ActorBox = Actor->GetComponentsBoundingBox(true);
	}

	if (!ActorBox.IsValid)
	{
		const FVector Loc = Actor->GetActorLocation();
		ActorBox = FBox(Loc - FVector(34.0f, 34.0f, 88.0f), Loc + FVector(34.0f, 34.0f, 88.0f));
	}

	for (auto& GridPair : DynamicSurfaceGrids)
	{
		FDynamicActorSurfaceGrid& Grid = GridPair.Value;
		AActor* DynActor = Grid.OwnerActor.Get();
		USceneComponent* TransformComp = Grid.TransformComponent.Get();

		if (!DynActor || !TransformComp || Grid.LocalCells.IsEmpty())
		{
			continue;
		}

		const FBox CompBox = TransformComp->Bounds.GetBox().ExpandBy(SafeCellSize);
		if (!CompBox.Intersect(ActorBox))
		{
			continue;
		}

		const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
		const FTransform InvRigidTransform = RigidTransform.Inverse();
		const FBox LocalActorBox = ActorBox.TransformBy(InvRigidTransform);
		constexpr float ContactMargin = SurfaceGridConstants::DefaultContactMargin;

		TArray<FSurfaceCellCoord> TouchedLocalCoords;
		for (const auto& CellPair : Grid.LocalCells)
		{
			const FSurfaceCellCoord& LocalCoord = CellPair.Key;
			const FSurfaceCellData& CellData = CellPair.Value;
			const FBox LocalSurfaceBox = SurfaceGridUtils::GetCellContactBox(LocalCoord, CellData.SurfaceLocation, SafeCellSize, ContactMargin);

			if (LocalActorBox.Intersect(LocalSurfaceBox))
			{
				TouchedLocalCoords.Add(LocalCoord);
			}
		}

		if (TouchedLocalCoords.IsEmpty())
		{
			continue;
		}

		// Faza A: Aktor wpływa na stykające się komórki dynamiczne (np. płonący gracz zapala olej na bramie)
		TArray<EStatusEffectType> ActorStatuses = StatusComp->GetActiveStatuses();
		UElementalReactionRules::SortByReactionPriority(ActorStatuses);
		const EPhysicalMaterialType DynMat = SurfaceGridGeometryUtils::GetMaterialFromActor(DynActor);

		SurfaceActorInteractionUtils::ApplyActorEffectsToFloor(
			Actor,
			StatusComp,
			TouchedLocalCoords,
			Grid.LocalCells,
			ActorStatuses,
			[&](const FSurfaceCellCoord& LocalCoord, EStatusEffectType Status, float Duration, AActor* Instigator)
			{
				return ApplyStatusToDynamicCell(
					DynActor,
					TransformComp,
					LocalCoord,
					Status,
					Duration,
					Instigator,
					DynMat,
					0,
					SafeCellSize,
					CurrentTime,
					OnCellChanged);
			});

		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			return;
		}

		// Faza B: Komórki dynamiczne wpływają na aktora (np. zaolejona brama brudzi gracza)
		SurfaceActorInteractionUtils::ApplyFloorEffectsToActor(
			Actor,
			StatusComp,
			TouchedLocalCoords,
			Grid.LocalCells,
			SafeCellSize,
			&RigidTransform);
	}
}

bool FDynamicSurfaceGridManager::CheckIfAnyGridMoved()
{
	bool bAnyMoved = false;
	for (auto& Pair : DynamicSurfaceGrids)
	{
		const USceneComponent* Comp = Pair.Value.TransformComponent.Get();
		if (!Comp)
		{
			continue;
		}

		const FTransform CurrentRigid = GetDynamicRigidTransform(Comp);
		if (!CurrentRigid.Equals(Pair.Value.LastTransform, 0.5f))
		{
			Pair.Value.LastTransform = CurrentRigid;
			bAnyMoved = true;
		}
	}
	return bAnyMoved;
}

void FDynamicSurfaceGridManager::DrawDebug(const UWorld* World, float SafeCellSize) const
{
#if !UE_BUILD_SHIPPING
	if (!World)
	{
		return;
	}

	// NOTE: TO BE REMOVED - ONLY FOR DEBUG AND PERFORMANCE TESTING PURPOSES
	// Prevents ULineBatchComponent from freezing the engine when 10,000+ cells are active across the map.
	FVector ViewLocation = FVector::ZeroVector;
	const bool bHasViewLocation = SurfaceGridGeometryUtils::GetDebugViewerLocation(World, ViewLocation);
	const float MaxDebugDrawDistSq = SurfaceGridConstants::MaxDebugDrawDistSq;

	const float DebugLifeTime = 0.25f;

	for (const auto& GridPair : DynamicSurfaceGrids)
	{
		const FDynamicActorSurfaceGrid& Grid = GridPair.Value;
		USceneComponent* TransformComp = Grid.TransformComponent.Get();
		if (!TransformComp)
		{
			continue;
		}

		const FTransform RigidTransform = GetDynamicRigidTransform(TransformComp);
		const FQuat CompRot = RigidTransform.GetRotation();

		for (const auto& CellPair : Grid.LocalCells)
		{
			const FSurfaceCellCoord& LocalCoord = CellPair.Key;
			const FSurfaceCellData& Data = CellPair.Value;

			if (Data.IsEmpty())
			{
				continue;
			}

			const FVector LocalCenter = LocalCoord.ToWorldLocation(SafeCellSize);
			const FVector LocalNormal = SurfaceGridUtils::FaceDirectionToNormal(LocalCoord.Face);

			// Pełny sześcian 3D reprezentujący całą objętość woksela (50x50x50 cm z lekkim marginesem na odstęp między komórkami)
			const FVector LocalHalfExtent = FVector(SafeCellSize * SurfaceGridConstants::NormalOffsetRatio);

			const FVector LocalBasePos = SurfaceGridUtils::GetSurfaceBaseLocation(LocalCoord, Data.SurfaceLocation, SafeCellSize);
			const FVector LocalVisualCenter = LocalBasePos + LocalNormal * (SafeCellSize * 0.5f);

			const FVector WorldVisualCenter = RigidTransform.TransformPosition(LocalVisualCenter);
			if (bHasViewLocation && FVector::DistSquared(WorldVisualCenter, ViewLocation) > MaxDebugDrawDistSq)
			{
				continue;
			}

			const FColor Color = USurfaceCellTransitionUtils::GetCellDebugColor(Data);

			// Komórki dynamiczne rysujemy z podwójnym sześcianem (concentric box) oraz pogrubioną linią,
			// aby natychmiast odróżnić je wizualnie od komórek statycznych architektury lochu.
			DrawDebugBox(World, WorldVisualCenter, LocalHalfExtent, CompRot, Color, false, DebugLifeTime, 0, 1.2f);
			DrawDebugBox(World, WorldVisualCenter, LocalHalfExtent * 0.6f, CompRot, Color, false, DebugLifeTime, 0, 0.0f);
		}
	}
#endif
}

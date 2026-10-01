#include "DynamicSurfaceGridManager.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

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

int32 FDynamicSurfaceGridManager::ClearCellsInBounds(const FBox& BoundingBox, float SafeCellSize)
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

void FDynamicSurfaceGridManager::ProcessActorInteractions(AActor* Actor, UStatusEffectComponent* StatusComp, float SafeCellSize)
{
	if (!Actor || Actor->IsActorBeingDestroyed() || !StatusComp || DynamicSurfaceGrids.IsEmpty())
	{
		return;
	}

	const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
	if (!ActorBox.IsValid)
	{
		return;
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

		TArray<FSurfaceCellCoord> TouchedLocalCoords;
		for (const auto& CellPair : Grid.LocalCells)
		{
			const FSurfaceCellCoord& LocalCoord = CellPair.Key;
			const FVector LocalCenter = LocalCoord.ToWorldLocation(SafeCellSize);
			const FVector WorldCenter = RigidTransform.TransformPosition(LocalCenter);
			const FBox CellWorldBox(WorldCenter - FVector(SafeCellSize * 0.5f), WorldCenter + FVector(SafeCellSize * 0.5f));

			if (CellWorldBox.Intersect(ActorBox))
			{
				TouchedLocalCoords.Add(LocalCoord);
			}
		}

		if (TouchedLocalCoords.IsEmpty())
		{
			continue;
		}

		SurfaceActorInteractionUtils::ApplyFloorEffectsToActor(
			Actor,
			StatusComp,
			TouchedLocalCoords,
			Grid.LocalCells,
			SafeCellSize);
	}
}

void FDynamicSurfaceGridManager::DrawDebug(const UWorld* World, float SafeCellSize) const
{
#if !UE_BUILD_SHIPPING
	if (!World)
	{
		return;
	}

	const float DebugLifeTime = 0.3f;

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

			const FColor Color = USurfaceCellTransitionUtils::GetCellDebugColor(Data);

			const FVector LocalCenter = LocalCoord.ToWorldLocation(SafeCellSize);
			const FVector LocalNormal = SurfaceGridUtils::FaceDirectionToNormal(LocalCoord.Face);
			const FVector LocalVisualCenter = LocalCenter + LocalNormal * 3.0f;

			const FVector WorldVisualCenter = RigidTransform.TransformPosition(LocalVisualCenter);
			const FVector HalfExtent = FVector(SafeCellSize * 0.42f);

			DrawDebugBox(World, WorldVisualCenter, HalfExtent, CompRot, Color, false, DebugLifeTime, 0, 2.0f);
		}
	}
#endif
}

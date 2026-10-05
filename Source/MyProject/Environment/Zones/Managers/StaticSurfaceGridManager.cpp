#include "StaticSurfaceGridManager.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridProjectionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceCellTransitionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceActorInteractionUtils.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Interfaces/SurfaceGridTargetInterface.h"
#include "MyProject/Logging/DungeonLogCategories.h"

bool FStaticSurfaceGridManager::GetSurfaceMaterialAtCoord(
	const UWorld* World,
	float SafeCellSize,
	const FSurfaceCellCoord& Coord,
	EPhysicalMaterialType& OutMaterial,
	AActor*& OutSurfaceActor,
	FVector& OutSurfaceLocation)
{
	OutSurfaceActor = nullptr;
	OutSurfaceLocation = FVector::ZeroVector;
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
	const FVector Center = Coord.ToWorldLocation(SafeCellSize);
	FHitResult Hit;
	const bool bHit = SurfaceGridGeometryUtils::ProbeSurfaceAt(World, Center, Normal, SafeCellSize * 0.8f, Hit, OutMaterial);
	if (bHit)
	{
		OutSurfaceActor = Hit.GetActor();
		OutSurfaceLocation = Hit.ImpactPoint;
	}
	return bHit;
}

bool FStaticSurfaceGridManager::ApplyStatusToCell(
	UWorld* World,
	const FSurfaceCellCoord& Coord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial,
	AActor* SurfaceActor,
	uint8 Tier,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged,
	const FVector& SurfaceLocation)
{
	if (!World || IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	FSurfaceCellData* Existing = ActiveCells.Find(Coord);
	EPhysicalMaterialType SurfaceMat = ExplicitMaterial;
	AActor* ResolvedSurfaceActor = SurfaceActor;
	FVector ResolvedLocation = SurfaceLocation;

	if (Existing)
	{
		SurfaceMat = Existing->SurfaceMaterial;
		if (!ResolvedSurfaceActor)
		{
			ResolvedSurfaceActor = Existing->SurfaceActor.Get();
		}
		if (ResolvedLocation.IsNearlyZero())
		{
			ResolvedLocation = Existing->SurfaceLocation;
		}
	}
	else
	{
		// Weryfikacja minimalnego pokrycia: jeśli przekazany aktor ma <50% pokrycia komórki (zasada większości),
		// reprobujemy w centrum, aby sprawdzić właściwego aktora pod spodem (np. kamień zamiast metalu).
		if (ResolvedSurfaceActor && !SurfaceGridGeometryUtils::HasSufficientSurfaceCoverage(ResolvedSurfaceActor, Coord, SafeCellSize))
		{
			ResolvedSurfaceActor = nullptr;
			ResolvedLocation = FVector::ZeroVector;
		}

		if (!ResolvedSurfaceActor)
		{
			AActor* ProbedActor = nullptr;
			FVector ProbedLocation = FVector::ZeroVector;
			EPhysicalMaterialType ProbedMat = EPhysicalMaterialType::Stone;
			if (!GetSurfaceMaterialAtCoord(World, SafeCellSize, Coord, ProbedMat, ProbedActor, ProbedLocation) ||
				(ProbedActor && !SurfaceGridGeometryUtils::HasSufficientSurfaceCoverage(ProbedActor, Coord, SafeCellSize)))
			{
				return false;
			}
			ResolvedSurfaceActor = ProbedActor;
			SurfaceMat = ProbedMat;
			ResolvedLocation = ProbedLocation;
		}
		else if (ResolvedLocation.IsNearlyZero())
		{
			ResolvedLocation = SurfaceGridUtils::GetFaceCenter(Coord, SafeCellSize);
		}

		// Dynamic Surface Safety Guard: komórki na ruchomych obiektach (np. bramach) NIE MOGĄ trafić do siatki statycznej!
		if (SurfaceGridGeometryUtils::IsDynamicSurfaceTarget(ResolvedSurfaceActor))
		{
			return false;
		}
	}

	return USurfaceCellTransitionUtils::ApplyStatusToCellInMap(
		ActiveCells,
		Coord,
		IncomingStatus,
		Duration,
		Instigator,
		SurfaceMat,
		ResolvedSurfaceActor,
		Tier,
		CurrentTime,
		OnCellChanged,
		ResolvedLocation);
}

int32 FStaticSurfaceGridManager::ApplyContinuousZoneToCells(
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
	if (!World || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	const float RadiusSq = FMath::Square(Radius);

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
		bool bWasAlreadyPresent = false;
	};
	TArray<FPendingZoneStaticCell> PendingCells;

	// KROK 1: Bezpieczne zebranie kandydatów w trybie Read-Only (brak modyfikacji TMap w pętli)
	for (const auto& Pair : ActiveCells)
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

		// KROK A: Jeśli komórka ma już ten status w co najmniej tym samym Tierze i ma wystarczający zapas czasu, pomijamy!
		const FSurfaceCellStatusEntry* ExistingEntry = Data.FindStatus(Status);
		if (ExistingEntry && ExistingEntry->Tier >= Tier)
		{
			if (ExistingEntry->IsPermanent() || ExistingEntry->GetRemainingDuration(CurrentTime) > SurfaceGridConstants::ContinuousZoneSkipThreshold)
			{
				continue;
			}
		}

		// KROK B: Odrzucenie komórek, które fizycznie nie mogą przyjąć tego statusu ani nie wejdą w reakcję żywiołową
		if (!UElementalReactionRules::CanApplyStatusToTarget(Data.SurfaceMaterial, Status, Data.GetStatusTypes()))
		{
			continue;
		}

		// KROK C: Test Line-of-Sight wykonujemy wyłącznie dla komórek realnie wymagających nałożenia/odświeżenia
		const FVector CellNormal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
		const FVector CellSurfacePos = CellWorldPos + CellNormal * (SafeCellSize * SurfaceGridConstants::NormalOffsetRatio);

		if (!SurfaceGridGeometryUtils::HasDirectBurstLineOfSight(World, Origin, CellSurfacePos, CellNormal, Data.SurfaceActor.Get(), LoSParams))
		{
			continue;
		}

		PendingCells.Add({ Coord, Data.SurfaceMaterial, Data.SurfaceActor.Get(), Data.SurfaceLocation, (ExistingEntry != nullptr) });
	}

	int32 AffectedCount = 0;
	int32 NewlyAdded = 0;

	// KROK 2: Aplikacja statusu po lokalnym TArray
	for (const FPendingZoneStaticCell& Pending : PendingCells)
	{
		if (ApplyStatusToCell(World, Pending.Coord, Status, Duration, Instigator, Pending.Material, Pending.SurfaceActor.Get(), Tier, SafeCellSize, CurrentTime, OnCellChanged, Pending.SurfaceLocation))
		{
			AffectedCount++;
			if (!Pending.bWasAlreadyPresent)
			{
				NewlyAdded++;
			}
		}
	}

	if (OutNewlyAddedCount)
	{
		*OutNewlyAddedCount = NewlyAdded;
	}

	return AffectedCount;
}

int32 FStaticSurfaceGridManager::ClearCellsInBounds(
	const FBox& BoundingBox,
	float SafeCellSize,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!BoundingBox.IsValid || ActiveCells.IsEmpty())
	{
		return 0;
	}

	int32 RemovedCount = 0;
	const FBox ExpandedBox = BoundingBox.ExpandBy(SafeCellSize * 0.5f);
	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		const FSurfaceCellCoord& Coord = It.Key();
		const FVector CellWorldCenter = Coord.ToWorldLocation(SafeCellSize);

		if (ExpandedBox.IsInsideOrOn(CellWorldCenter))
		{
			OnCellChanged(Coord, EStatusEffectType::None, nullptr);
			It.RemoveCurrent();
			RemovedCount++;
		}
	}

	return RemovedCount;
}

void FStaticSurfaceGridManager::ExpireCells(
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	USurfaceCellTransitionUtils::ExpireCellsInMap(ActiveCells, CurrentTime, OnCellChanged);
}

void FStaticSurfaceGridManager::ProcessSolidFuelCombustion(
	UWorld* World,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!World || ActiveCells.IsEmpty())
	{
		return;
	}

	struct FPendingFuelSpread
	{
		FSurfaceCellCoord Coord;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Wood;
		TWeakObjectPtr<AActor> SurfaceActor = nullptr;
		TWeakObjectPtr<AActor> Instigator = nullptr;
		uint8 Tier = 0;
	};

	TMap<FSurfaceCellCoord, FPendingFuelSpread> PendingFuelSpreads;

	for (auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& SourceCoord = Pair.Key;
		FSurfaceCellData& SourceData = Pair.Value;

		const FSurfaceCellStatusEntry* BurningEntry = SourceData.FindStatus(EStatusEffectType::Burning);
		if (!BurningEntry)
		{
			SourceData.NextFuelSpreadTime = 0.0f;
			continue;
		}

		const FPhysicalMaterialTraits Traits = PhysicalMaterialUtils::GetTraits(SourceData.SurfaceMaterial);
		if (!Traits.bSelfSustainingFuel)
		{
			continue;
		}

		if (SourceData.NextFuelSpreadTime <= 0.0f)
		{
			SourceData.NextFuelSpreadTime = CurrentTime + Traits.FuelSpreadInterval;
			continue;
		}

		if (CurrentTime < SourceData.NextFuelSpreadTime)
		{
			continue;
		}

		SourceData.NextFuelSpreadTime = CurrentTime + Traits.FuelSpreadInterval;

		TArray<SurfaceGridGeometryUtils::FSurfaceSpreadCandidate> Candidates;
		SurfaceGridGeometryUtils::FindSpreadCandidates(World, SourceCoord, SourceData.SurfaceActor.Get(), ActiveCells, SafeCellSize, Candidates);

		for (const auto& Candidate : Candidates)
		{
			if (PhysicalMaterialUtils::GetTraits(Candidate.Material).bSelfSustainingFuel)
			{
				const FSurfaceCellData* ExistingData = ActiveCells.Find(Candidate.Coord);
				if (!ExistingData || !ExistingData->HasStatus(EStatusEffectType::Burning))
				{
					if (!PendingFuelSpreads.Contains(Candidate.Coord))
					{
						FPendingFuelSpread Spread;
						Spread.Coord = Candidate.Coord;
						Spread.Material = Candidate.Material;
						Spread.SurfaceActor = Candidate.SurfaceActor;
						Spread.Instigator = BurningEntry->Instigator;
						Spread.Tier = BurningEntry->Tier;
						PendingFuelSpreads.Add(Candidate.Coord, Spread);
					}
				}
			}
		}
	}

	const float BaseDuration = UElementalReactionRules::GetEffectConfig(EStatusEffectType::Burning).GetBaseDuration();
	for (const auto& Pair : PendingFuelSpreads)
	{
		const FPendingFuelSpread& Spread = Pair.Value;
		ApplyStatusToCell(World, Spread.Coord, EStatusEffectType::Burning, BaseDuration, Spread.Instigator.Get(), Spread.Material, Spread.SurfaceActor.Get(), Spread.Tier, SafeCellSize, CurrentTime, OnCellChanged);
	}
}

void FStaticSurfaceGridManager::ProcessActorInteraction(
	AActor* Actor,
	UStatusEffectComponent* StatusComp,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<bool(const FSurfaceCellCoord&, EStatusEffectType, float, AActor*)> ApplyToCellCallback)
{
	if (!Actor || Actor->IsActorBeingDestroyed() || !StatusComp)
	{
		return;
	}

	TArray<FSurfaceCellCoord> TouchedCells;
	SurfaceGridProjectionUtils::GetCellsTouchingActor(Actor, ActiveCells, SafeCellSize, TouchedCells);
	if (TouchedCells.Num() == 0)
	{
		return;
	}

	TArray<EStatusEffectType> ActorStatuses = StatusComp->GetActiveStatuses();
	UElementalReactionRules::SortByReactionPriority(ActorStatuses);
	SurfaceActorInteractionUtils::ApplyActorEffectsToFloor(
		Actor,
		StatusComp,
		TouchedCells,
		ActiveCells,
		ActorStatuses,
		ApplyToCellCallback);

	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
	{
		return;
	}

	SurfaceActorInteractionUtils::ApplyFloorEffectsToActor(Actor, StatusComp, TouchedCells, ActiveCells, SafeCellSize);
}

void FStaticSurfaceGridManager::DrawDebug(const UWorld* World, float SafeCellSize) const
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

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FSurfaceCellData& Data = Pair.Value;

		if (Data.IsEmpty())
		{
			continue;
		}

		const FVector Center = Coord.ToWorldLocation(SafeCellSize);
		if (bHasViewLocation && FVector::DistSquared(Center, ViewLocation) > MaxDebugDrawDistSq)
		{
			continue;
		}

		const FColor Color = USurfaceCellTransitionUtils::GetCellDebugColor(Data);
		const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);

		// Pełny sześcian 3D reprezentujący całą objętość woksela (50x50x50 cm z lekkim marginesem na odstęp między komórkami)
		const FVector HalfExtent = FVector(SafeCellSize * SurfaceGridConstants::NormalOffsetRatio);

		FVector BasePos;
		if (!Data.SurfaceLocation.IsNearlyZero())
		{
			// Płaszczyzna z fizycznej kolizji na architekturze lochu (zapobiega chowaniu się komórek w suficie/ścianie)
			const float SurfacePlaneDist = FVector::DotProduct(Data.SurfaceLocation, Normal);
			const float GridPlaneDist = FVector::DotProduct(Center, Normal);
			BasePos = Center + Normal * (SurfacePlaneDist - GridPlaneDist);
		}
		else
		{
			// Fallback: środek zewnętrznej ściany woksela
			BasePos = SurfaceGridUtils::GetFaceCenter(Coord, SafeCellSize);
		}

		// Środek sześcianu 3D umieszczony w powietrzu przed powierzchnią architektury
		const FVector VisualCenter = BasePos + Normal * (SafeCellSize * 0.5f);

		DrawDebugBox(World, VisualCenter, HalfExtent, Color, false, DebugLifeTime, 0, 0.0f);
	}
#endif
}

#include "StaticSurfaceGridManager.h"

#include "DynamicSurfaceGridManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

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
	AActor*& OutSurfaceActor)
{
	OutSurfaceActor = nullptr;
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
	const FVector Center = Coord.ToWorldLocation(SafeCellSize);
	FHitResult Hit;
	const bool bHit = SurfaceGridGeometryUtils::ProbeSurfaceAt(World, Center, Normal, SafeCellSize * 0.8f, Hit, OutMaterial);
	if (bHit)
	{
		OutSurfaceActor = Hit.GetActor();
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
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!World || IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	FSurfaceCellData* Existing = ActiveCells.Find(Coord);
	EPhysicalMaterialType SurfaceMat = ExplicitMaterial;
	AActor* ResolvedSurfaceActor = SurfaceActor;

	if (!Existing)
	{
		AActor* ProbedActor = nullptr;
		if (!GetSurfaceMaterialAtCoord(World, SafeCellSize, Coord, SurfaceMat, ProbedActor))
		{
			UE_LOG(LogDungeonElements, Verbose, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) rejected: No valid surface geometry!"),
				Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face));
			return false;
		}
		if (!ResolvedSurfaceActor)
		{
			ResolvedSurfaceActor = ProbedActor;
		}
	}
	else
	{
		SurfaceMat = Existing->SurfaceMaterial;
		if (!ResolvedSurfaceActor)
		{
			ResolvedSurfaceActor = Existing->SurfaceActor.Get();
		}
	}
	// Dynamic Surface Safety Guard: komórki na ruchomych obiektach (np. bramach) NIE MOGĄ trafić do siatki statycznej!
	if (ResolvedSurfaceActor && ResolvedSurfaceActor->Implements<USurfaceGridTargetInterface>()
		&& ISurfaceGridTargetInterface::Execute_IsDynamicSurface(ResolvedSurfaceActor))
	{
		if (Existing)
		{
			ActiveCells.Remove(Coord);
		}
		return false;
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
		OnCellChanged);
}

int32 FStaticSurfaceGridManager::ClearCellsInBounds(
	const FBox& BoundingBox,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!BoundingBox.IsValid || ActiveCells.IsEmpty())
	{
		return 0;
	}

	int32 RemovedCount = 0;
	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		const FSurfaceCellCoord& Coord = It.Key();
		const FVector CellWorldCenter = Coord.ToWorldLocation(50.0f); // Standard grid center probe

		if (BoundingBox.IsInsideOrOn(CellWorldCenter))
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

void FStaticSurfaceGridManager::ProcessSurfaceStructuralDamage(
	UWorld* World,
	const FDynamicSurfaceGridManager& DynamicGridManager,
	float DeltaTime)
{
	if (!World || (ActiveCells.IsEmpty() && DynamicGridManager.GetGrids().IsEmpty()) || DeltaTime <= 0.0f)
	{
		return;
	}

	struct FDamageAggregate
	{
		float TotalDPS = 0.0f;
		TWeakObjectPtr<AActor> LastInstigator = nullptr;
	};

	TMap<TPair<TWeakObjectPtr<AActor>, EDamageType>, FDamageAggregate> AggregatedDamage;
	constexpr float MaxStructuralDPS = 25.0f;

	auto CollectDamageFromCell = [&](const FSurfaceCellData& Cell)
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

	for (const auto& Pair : ActiveCells)
	{
		CollectDamageFromCell(Pair.Value);
	}

	for (const auto& GridPair : DynamicGridManager.GetGrids())
	{
		for (const auto& CellPair : GridPair.Value.LocalCells)
		{
			CollectDamageFromCell(CellPair.Value);
		}
	}

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

		DmgComp->ApplyDamage(DamageAmount, DmgType, Agg.LastInstigator.Get());
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

	const float DebugLifeTime = 0.3f;

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FSurfaceCellData& Data = Pair.Value;

		if (Data.IsEmpty())
		{
			continue;
		}

		const FColor Color = USurfaceCellTransitionUtils::GetCellDebugColor(Data);

		const FVector Center = Coord.ToWorldLocation(SafeCellSize);
		const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
		const FVector VisualCenter = Center + Normal * 3.0f;
		const FVector HalfExtent = FVector(SafeCellSize * 0.42f);

		DrawDebugBox(World, VisualCenter, HalfExtent, Color, false, DebugLifeTime, 0, 2.0f);
	}
#endif
}

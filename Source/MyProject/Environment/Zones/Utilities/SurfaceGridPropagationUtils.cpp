#include "SurfaceGridPropagationUtils.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

#include "MyProject/Environment/Zones/Managers/StaticSurfaceGridManager.h"
#include "MyProject/Environment/Zones/Managers/DynamicSurfaceGridManager.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"

namespace
{
	/** Reprezentuje zakolejkowane rozprzestrzenienie statusu na sąsiada w siatce */
	struct FPendingSpreadCell
	{
		FSurfaceCellCoord Coord;
		EStatusEffectType NewStatus = EStatusEffectType::None;
		float Duration = 0.0f;
		TWeakObjectPtr<AActor> Instigator = nullptr;
	};

	/** Ewaluuje możliwość rozprzestrzenienia pojedynczego statusu ze źródła na sąsiada */
	static bool TryEvaluateSpreadReaction(
		const FSurfaceCellCoord& NeighborCoord,
		const FSurfaceCellData& NeighborData,
		const FSurfaceCellStatusEntry& SourceEntry,
		float CurrentTime,
		FPendingSpreadCell& OutSpread)
	{
		const float RemainingSourceTime = SourceEntry.GetRemainingDuration(CurrentTime);
		if (!SourceEntry.IsPermanent() && RemainingSourceTime <= 0.1f)
		{
			return false;
		}

		for (const FSurfaceCellStatusEntry& NeighborEntry : NeighborData.ActiveStatuses)
		{
			if (SourceEntry.Status == NeighborEntry.Status)
			{
				continue;
			}

			FElementalReactionResult SpreadReaction;
			if (UElementalReactionRules::CanSpreadToNeighbor(SourceEntry.Status, NeighborEntry.Status, SpreadReaction))
			{
				const EStatusEffectType TargetStatus = (SpreadReaction.ResultingStatus != EStatusEffectType::None) ? SpreadReaction.ResultingStatus : SourceEntry.Status;

				// Bezpiecznik fizyczny: jeśli sąsiad ma już ten status, nie rozprzestrzeniaj go ponownie
				if (NeighborData.HasStatus(TargetStatus))
				{
					continue;
				}

				const float FallbackSpreadDuration = UElementalReactionRules::GetEffectConfig(TargetStatus).GetBaseDuration();
				float CalculatedDuration = (SpreadReaction.ResultingDuration > 0.0f) ? SpreadReaction.ResultingDuration : FallbackSpreadDuration;

				if (SpreadReaction.bSyncWithCarrierDuration)
				{
					if (NeighborEntry.IsPermanent())
					{
						CalculatedDuration = FallbackSpreadDuration;
					}
					else
					{
						const float NeighborCarrierRemaining = NeighborEntry.GetRemainingDuration(CurrentTime);
						if (NeighborCarrierRemaining > 0.0f)
						{
							CalculatedDuration = NeighborCarrierRemaining;
						}
					}
				}
				else if (!SourceEntry.IsPermanent() && RemainingSourceTime > 0.0f)
				{
					CalculatedDuration = FMath::Min(CalculatedDuration, RemainingSourceTime);
				}

				OutSpread.Coord = NeighborCoord;
				OutSpread.NewStatus = TargetStatus;
				OutSpread.Duration = CalculatedDuration;
				OutSpread.Instigator = SourceEntry.Instigator;
				return true;
			}
		}

		return false;
	}
}

void FSurfaceGridPropagationUtils::PropagateElementalSpreads(
	UWorld* World,
	FStaticSurfaceGridManager& StaticGrid,
	FDynamicSurfaceGridManager& DynamicGrid,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells = StaticGrid.GetActiveCells();
	const TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& DynamicGrids = DynamicGrid.GetGrids();

	if (ActiveCells.IsEmpty() && DynamicGrids.IsEmpty())
	{
		return;
	}

	struct FPendingDynamicSpread
	{
		TWeakObjectPtr<AActor> DynActor;
		TWeakObjectPtr<USceneComponent> TransformComp;
		FSurfaceCellCoord LocalCoord;
		EStatusEffectType NewStatus = EStatusEffectType::None;
		float Duration = 0.0f;
		TWeakObjectPtr<AActor> Instigator = nullptr;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
	};

	TMap<FSurfaceCellCoord, FPendingSpreadCell> PendingStaticSpreads;
	TArray<FPendingDynamicSpread> PendingDynamicSpreads;
	TArray<FSurfaceCellCoord> NeighborCoords;
	const float MaxContactDistSq = FMath::Square(SafeCellSize * 1.5f);

	// Data-driven predykat: sprawdzamy czy jakikolwiek status w komórce ma skonfigurowaną regułę rozprzestrzeniania
	auto HasSpreadingStatus = [](const FSurfaceCellData& Cell) -> bool
	{
		for (const FSurfaceCellStatusEntry& Entry : Cell.ActiveStatuses)
		{
			if (UElementalReactionRules::CanStatusSpread(Entry.Status))
			{
				return true;
			}
		}
		return false;
	};

	// Wspólna funkcja ewaluująca reakcję rozprzestrzeniania z komórki źródłowej na docelową
	auto TrySpreadBetweenCells = [&](const FSurfaceCellData& SrcData, const FSurfaceCellCoord& DstCoord, const FSurfaceCellData& DstData, FPendingSpreadCell& OutSpread) -> bool
	{
		for (const FSurfaceCellStatusEntry& SrcEntry : SrcData.ActiveStatuses)
		{
			if (TryEvaluateSpreadReaction(DstCoord, DstData, SrcEntry, CurrentTime, OutSpread))
			{
				return true;
			}
		}
		return false;
	};

	// -------------------------------------------------------------------------
	// 1. Statyczne komórki świata (ActiveCells)
	// -------------------------------------------------------------------------
	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& SourceCoord = Pair.Key;
		const FSurfaceCellData& SourceData = Pair.Value;

		if (SourceData.IsEmpty() || !HasSpreadingStatus(SourceData))
		{
			continue;
		}

		const FVector SourcePos = SourceCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(SourceCoord.Face) * (SafeCellSize * 0.45f);
		SourceCoord.GetAdjacentNeighbors(NeighborCoords);

		for (const FSurfaceCellCoord& NeighborCoord : NeighborCoords)
		{
			if (NeighborCoord.X == SourceCoord.X && NeighborCoord.Y == SourceCoord.Y && NeighborCoord.Z == SourceCoord.Z)
			{
				continue;
			}

			// A. Sąsiad w statycznej siatce świata
			if (const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord))
			{
				if (NeighborData->IsEmpty()) continue;
				if (NeighborCoord.Face != SourceCoord.Face && NeighborData->SurfaceActor == SourceData.SurfaceActor) continue;

				const FVector NeighborPos = NeighborCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(NeighborCoord.Face) * (SafeCellSize * 0.45f);
				if (FVector::DistSquared(SourcePos, NeighborPos) > MaxContactDistSq) continue;

				FPendingSpreadCell Spread;
				if (TrySpreadBetweenCells(SourceData, NeighborCoord, *NeighborData, Spread))
				{
					if (FPendingSpreadCell* Existing = PendingStaticSpreads.Find(NeighborCoord))
					{
						if (Spread.NewStatus == Existing->NewStatus)
						{
							Existing->Duration = (Spread.Duration == 0.0f || Existing->Duration == 0.0f)
								? 0.0f : FMath::Max(Existing->Duration, Spread.Duration);
						}
					}
					else
					{
						PendingStaticSpreads.Add(NeighborCoord, Spread);
					}
				}
				continue;
			}

			// B. Sąsiad w dynamicznej siatce ruchomego aktora
			const FVector NeighborWorldPos = NeighborCoord.ToWorldLocation(SafeCellSize);
			const FVector NeighborWorldNorm = SurfaceGridUtils::FaceDirectionToNormal(NeighborCoord.Face);

			for (const auto& GridPair : DynamicGrids)
			{
				const FDynamicActorSurfaceGrid& DynGrid = GridPair.Value;
				USceneComponent* TransformComp = DynGrid.TransformComponent.Get();
				if (!DynGrid.OwnerActor.IsValid() || !TransformComp || DynGrid.LocalCells.IsEmpty()) continue;

				// Broad-phase: szybkie odrzucenie odległych aktorów
				if (!TransformComp->Bounds.GetBox().ExpandBy(SafeCellSize * 1.5f).IsInsideOrOn(NeighborWorldPos)) continue;

				const FTransform RigidTransform = FDynamicSurfaceGridManager::GetDynamicRigidTransform(TransformComp);
				const FVector LocalPos = RigidTransform.InverseTransformPosition(NeighborWorldPos);
				const FVector LocalNorm = RigidTransform.InverseTransformVector(NeighborWorldNorm);
				const FSurfaceCellCoord DynLocalCoord = FSurfaceCellCoord::FromWorldLocation(LocalPos, LocalNorm, SafeCellSize);

				if (const FSurfaceCellData* DynNeighborData = DynGrid.LocalCells.Find(DynLocalCoord))
				{
					if (DynNeighborData->IsEmpty()) continue;

					const FVector DynLocalPos = DynLocalCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(DynLocalCoord.Face) * (SafeCellSize * 0.45f);
					if (FVector::DistSquared(SourcePos, RigidTransform.TransformPosition(DynLocalPos)) > MaxContactDistSq) continue;

					FPendingSpreadCell Spread;
					if (TrySpreadBetweenCells(SourceData, DynLocalCoord, *DynNeighborData, Spread))
					{
						PendingDynamicSpreads.Add({
							DynGrid.OwnerActor.Get(),
							TransformComp,
							Spread.Coord,
							Spread.NewStatus,
							Spread.Duration,
							Spread.Instigator.Get(),
							DynNeighborData->SurfaceMaterial
						});
					}
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// 2. Dynamiczne komórki ruchomych obiektów (DynamicSurfaceGrids)
	// -------------------------------------------------------------------------
	for (const auto& GridPair : DynamicGrids)
	{
		const FDynamicActorSurfaceGrid& DynGrid = GridPair.Value;
		AActor* DynActor = DynGrid.OwnerActor.Get();
		USceneComponent* TransformComp = DynGrid.TransformComponent.Get();
		if (!DynActor || !TransformComp || DynGrid.LocalCells.IsEmpty()) continue;

		const FTransform RigidTransform = FDynamicSurfaceGridManager::GetDynamicRigidTransform(TransformComp);

		for (const auto& CellPair : DynGrid.LocalCells)
		{
			const FSurfaceCellCoord& LocalSourceCoord = CellPair.Key;
			const FSurfaceCellData& LocalSourceData = CellPair.Value;
			if (LocalSourceData.IsEmpty() || !HasSpreadingStatus(LocalSourceData)) continue;

			// A. Rozprzestrzenianie po lokalnej siatce mesha
			LocalSourceCoord.GetAdjacentNeighbors(NeighborCoords);
			for (const FSurfaceCellCoord& LocalNeighborCoord : NeighborCoords)
			{
				if (LocalNeighborCoord.X == LocalSourceCoord.X && LocalNeighborCoord.Y == LocalSourceCoord.Y && LocalNeighborCoord.Z == LocalSourceCoord.Z) continue;

				if (const FSurfaceCellData* LocalNeighborData = DynGrid.LocalCells.Find(LocalNeighborCoord))
				{
					if (LocalNeighborData->IsEmpty()) continue;

					FPendingSpreadCell Spread;
					if (TrySpreadBetweenCells(LocalSourceData, LocalNeighborCoord, *LocalNeighborData, Spread))
					{
						PendingDynamicSpreads.Add({
							DynActor,
							TransformComp,
							LocalNeighborCoord,
							Spread.NewStatus,
							Spread.Duration,
							Spread.Instigator.Get(),
							LocalNeighborData->SurfaceMaterial
						});
					}
				}
			}

			// B. Rozprzestrzenianie na sąsiadujące podłoże statyczne świata
			const FVector LocalContact = LocalSourceCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(LocalSourceCoord.Face) * (SafeCellSize * 0.45f);
			const FVector WorldContact = RigidTransform.TransformPosition(LocalContact);
			const FVector WorldNorm = RigidTransform.TransformVector(SurfaceGridUtils::FaceDirectionToNormal(LocalSourceCoord.Face)).GetSafeNormal();

			const FSurfaceCellCoord StaticWorldCoord = FSurfaceCellCoord::FromWorldLocation(WorldContact, WorldNorm, SafeCellSize);
			StaticWorldCoord.GetAdjacentNeighbors(NeighborCoords);

			for (const FSurfaceCellCoord& StaticNeighborCoord : NeighborCoords)
			{
				if (const FSurfaceCellData* StaticNeighborData = ActiveCells.Find(StaticNeighborCoord))
				{
					if (StaticNeighborData->IsEmpty()) continue;

					const FVector StaticNeighborPos = StaticNeighborCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(StaticNeighborCoord.Face) * (SafeCellSize * 0.45f);
					if (FVector::DistSquared(WorldContact, StaticNeighborPos) > MaxContactDistSq) continue;

					FPendingSpreadCell Spread;
					if (TrySpreadBetweenCells(LocalSourceData, StaticNeighborCoord, *StaticNeighborData, Spread))
					{
						if (FPendingSpreadCell* Existing = PendingStaticSpreads.Find(StaticNeighborCoord))
						{
							if (Spread.NewStatus == Existing->NewStatus)
							{
								Existing->Duration = (Spread.Duration == 0.0f || Existing->Duration == 0.0f)
									? 0.0f : FMath::Max(Existing->Duration, Spread.Duration);
							}
						}
						else
						{
							PendingStaticSpreads.Add(StaticNeighborCoord, Spread);
						}
					}
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// 3. Aplikujemy zakolejkowane dynamiczne rozprzestrzenienia
	// -------------------------------------------------------------------------
	for (const FPendingDynamicSpread& Spread : PendingDynamicSpreads)
	{
		if (Spread.DynActor.IsValid() && Spread.TransformComp.IsValid())
		{
			DynamicGrid.ApplyStatusToDynamicCell(
				Spread.DynActor.Get(),
				Spread.TransformComp.Get(),
				Spread.LocalCoord,
				Spread.NewStatus,
				Spread.Duration,
				Spread.Instigator.Get(),
				Spread.Material,
				0,
				SafeCellSize,
				CurrentTime,
				OnCellChanged);
		}
	}

	// -------------------------------------------------------------------------
	// 4. Aplikujemy zakolejkowane statyczne rozprzestrzenienia świata
	// -------------------------------------------------------------------------
	for (const auto& Pair : PendingStaticSpreads)
	{
		const FPendingSpreadCell& Spread = Pair.Value;
		StaticGrid.ApplyStatusToCell(
			World,
			Pair.Key,
			Spread.NewStatus,
			Spread.Duration,
			Spread.Instigator.Get(),
			EPhysicalMaterialType::Stone,
			nullptr,
			0,
			SafeCellSize,
			CurrentTime,
			OnCellChanged);
	}
}

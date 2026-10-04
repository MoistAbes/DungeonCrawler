#include "SurfaceGridPropagationUtils.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

#include "MyProject/Environment/Zones/Managers/StaticSurfaceGridManager.h"
#include "MyProject/Environment/Zones/Managers/DynamicSurfaceGridManager.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"
#include "MyProject/Shared/Interfaces/SurfaceGridTargetInterface.h"

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
	bool bConductionNetworkDirty,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells = StaticGrid.GetActiveCells();
	const TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& DynamicGrids = DynamicGrid.GetGrids();

	if (ActiveCells.IsEmpty() && DynamicGrids.IsEmpty())
	{
		return;
	}

	// 0. Błyskawiczna propagacja sieci przewodzącej (wykonywana tylko gdy stan sieci uległ zmianie lub istnieją ruchome obiekty dynamiczne)
	if (bConductionNetworkDirty || !DynamicGrids.IsEmpty())
	{
		PropagateConductionNetworks(World, StaticGrid, DynamicGrid, SafeCellSize, CurrentTime, OnCellChanged);
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

		// Globalna optymalizacja aktywnego frontiera (Active Frontier Optimization):
		// Sprawdzamy czy komórka ma przynajmniej jednego sąsiada, który realnie może przyjąć rozprzestrzenienie.
		// Wnętrze nasyconej plamy (np. ogień na oleju, gdzie wszyscy sąsiedzi już płoną) jest w równowadze i zostaje pominięte bez raycastów.
		bool bCanPotentiallySpread = false;
		TArray<FSurfaceCellCoord> CoplanarNeighbors;
		SourceCoord.GetCoplanarNeighbors(CoplanarNeighbors);

		for (const FSurfaceCellCoord& NeighborCoord : CoplanarNeighbors)
		{
			const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord);
			if (!NeighborData || NeighborData->IsEmpty())
			{
				// Sąsiad poza ActiveCells może być granicą z dynamicznym obiektem (np. brama)
				if (!DynamicGrids.IsEmpty())
				{
					bCanPotentiallySpread = true;
					break;
				}
				continue;
			}

			// Sprawdzamy czy sąsiad w ActiveCells może wejść w reakcję z którymkolwiek rozprzestrzeniającym się statusem źródła
			for (const FSurfaceCellStatusEntry& SrcEntry : SourceData.ActiveStatuses)
			{
				if (!UElementalReactionRules::CanStatusSpread(SrcEntry.Status))
				{
					continue;
				}

				for (const FSurfaceCellStatusEntry& DstEntry : NeighborData->ActiveStatuses)
				{
					FElementalReactionResult SpreadReaction;
					if (UElementalReactionRules::CanSpreadToNeighbor(SrcEntry.Status, DstEntry.Status, SpreadReaction))
					{
						const EStatusEffectType TargetStatus = (SpreadReaction.ResultingStatus != EStatusEffectType::None) ? SpreadReaction.ResultingStatus : SrcEntry.Status;
						if (!NeighborData->HasStatus(TargetStatus))
						{
							bCanPotentiallySpread = true;
							break;
						}
					}
				}

				if (bCanPotentiallySpread)
				{
					break;
				}
			}

			if (bCanPotentiallySpread)
			{
				break;
			}
		}

		if (!bCanPotentiallySpread)
		{
			continue;
		}

		const FVector SourcePos = SourceCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(SourceCoord.Face) * (SafeCellSize * 0.45f);

		TArray<SurfaceGridGeometryUtils::FSurfaceSpreadCandidate> Candidates;
		SurfaceGridGeometryUtils::FindSpreadCandidates(World, SourceCoord, SourceData.SurfaceActor.Get(), ActiveCells, SafeCellSize, Candidates);

		for (const auto& Candidate : Candidates)
		{
			// A. Sąsiad w statycznej siatce świata
			if (const FSurfaceCellData* NeighborData = ActiveCells.Find(Candidate.Coord))
			{
				if (NeighborData->IsEmpty()) continue;

				FPendingSpreadCell Spread;
				if (TrySpreadBetweenCells(SourceData, Candidate.Coord, *NeighborData, Spread))
				{
					if (FPendingSpreadCell* Existing = PendingStaticSpreads.Find(Candidate.Coord))
					{
						if (Spread.NewStatus == Existing->NewStatus)
						{
							Existing->Duration = (Spread.Duration == 0.0f || Existing->Duration == 0.0f)
								? 0.0f : FMath::Max(Existing->Duration, Spread.Duration);
						}
					}
					else
					{
						PendingStaticSpreads.Add(Candidate.Coord, Spread);
					}
				}
				continue;
			}

			// B. Sąsiad w dynamicznej siatce ruchomego aktora
			const FVector NeighborWorldPos = Candidate.Coord.ToWorldLocation(SafeCellSize);
			const FVector NeighborWorldNorm = SurfaceGridUtils::FaceDirectionToNormal(Candidate.Coord.Face);

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
			LocalSourceCoord.GetCoplanarNeighbors(NeighborCoords);
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
			NeighborCoords.Reset();
			NeighborCoords.Add(StaticWorldCoord);
			StaticWorldCoord.GetCoplanarNeighbors(NeighborCoords);

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

void FSurfaceGridPropagationUtils::PropagateConductionNetworks(
	UWorld* World,
	FStaticSurfaceGridManager& StaticGrid,
	FDynamicSurfaceGridManager& DynamicGrid,
	float SafeCellSize,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	if (!World)
	{
		return;
	}

	const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells = StaticGrid.GetActiveCells();
	const TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& DynamicGrids = DynamicGrid.GetGrids();

	if (ActiveCells.IsEmpty() && DynamicGrids.IsEmpty())
	{
		return;
	}

	struct FConductionItem
	{
		bool bIsDynamic = false;
		FSurfaceCellCoord Coord;
		TWeakObjectPtr<AActor> DynActor = nullptr;
		TWeakObjectPtr<USceneComponent> TransformComp = nullptr;
		EStatusEffectType Status = EStatusEffectType::None;
		float ServerEndTime = 0.0f;
		uint8 Tier = 0;
		TWeakObjectPtr<AActor> Instigator = nullptr;
	};

	TArray<FConductionItem> Queue;
	TMap<FSurfaceCellCoord, float> StaticVisitedEndTime;
	TMap<TPair<TWeakObjectPtr<AActor>, FSurfaceCellCoord>, float> DynamicVisitedEndTime;

	// 1. Zbieramy wszystkie aktywne źródła prądu / statusów o błyskawicznym przewodnictwie sieciowym
	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FSurfaceCellData& Data = Pair.Value;
		if (Data.IsEmpty()) continue;

		for (const FSurfaceCellStatusEntry& Entry : Data.ActiveStatuses)
		{
			if (UElementalReactionRules::IsInstantConduction(Entry.Status) && (Entry.IsPermanent() || Entry.GetRemainingDuration(CurrentTime) > 0.05f))
			{
				StaticVisitedEndTime.Add(Coord, Entry.ServerEndTime);

				// Globalna optymalizacja ziaren BFS (Frontier BFS Seeding):
				// Jeśli komórka znajduje się głęboko we wnętrzu nasyconej sieci (wszyscy jej 4 sąsiedzi w ActiveCells
				// mają już ten sam status ze zsynchronizowanym czasem), to nie ma dokąd popłynąć i nie dodajemy jej do kolejki BFS.
				bool bIsSaturatedInterior = true;
				TArray<FSurfaceCellCoord> CoplanarNeighbors;
				Coord.GetCoplanarNeighbors(CoplanarNeighbors);

				for (const FSurfaceCellCoord& NeighborCoord : CoplanarNeighbors)
				{
					const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord);
					if (!NeighborData || NeighborData->IsEmpty())
					{
						bIsSaturatedInterior = false;
						break;
					}

					const FSurfaceCellStatusEntry* NeighborStatus = NeighborData->FindStatus(Entry.Status);
					if (!NeighborStatus || (!NeighborStatus->IsPermanent() && NeighborStatus->ServerEndTime < Entry.ServerEndTime - 0.05f))
					{
						bIsSaturatedInterior = false;
						break;
					}
				}

				if (!bIsSaturatedInterior || !DynamicGrids.IsEmpty())
				{
					Queue.Add({ false, Coord, nullptr, nullptr, Entry.Status, Entry.ServerEndTime, Entry.Tier, Entry.Instigator });
				}
			}
		}
	}

	for (const auto& GridPair : DynamicGrids)
	{
		AActor* DynActor = GridPair.Key.Get();
		USceneComponent* TransformComp = GridPair.Value.TransformComponent.Get();
		if (!DynActor || !TransformComp || GridPair.Value.LocalCells.IsEmpty()) continue;

		for (const auto& CellPair : GridPair.Value.LocalCells)
		{
			const FSurfaceCellCoord& LocalCoord = CellPair.Key;
			const FSurfaceCellData& Data = CellPair.Value;
			if (Data.IsEmpty()) continue;

			for (const FSurfaceCellStatusEntry& Entry : Data.ActiveStatuses)
			{
				if (UElementalReactionRules::IsInstantConduction(Entry.Status) && (Entry.IsPermanent() || Entry.GetRemainingDuration(CurrentTime) > 0.05f))
				{
					DynamicVisitedEndTime.Add({ DynActor, LocalCoord }, Entry.ServerEndTime);
					Queue.Add({ true, LocalCoord, DynActor, TransformComp, Entry.Status, Entry.ServerEndTime, Entry.Tier, Entry.Instigator });
				}
			}
		}
	}

	if (Queue.IsEmpty())
	{
		return;
	}

	struct FPendingStaticConduction
	{
		FSurfaceCellCoord Coord;
		EStatusEffectType Status = EStatusEffectType::None;
		float Duration = 0.0f;
		TWeakObjectPtr<AActor> Instigator = nullptr;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
		TWeakObjectPtr<AActor> SurfaceActor = nullptr;
		uint8 Tier = 0;
	};
	TMap<FSurfaceCellCoord, FPendingStaticConduction> PendingStaticConductions;

	struct FPendingDynConduction
	{
		TWeakObjectPtr<AActor> DynActor;
		TWeakObjectPtr<USceneComponent> TransformComp;
		FSurfaceCellCoord LocalCoord;
		EStatusEffectType Status = EStatusEffectType::None;
		float Duration = 0.0f;
		TWeakObjectPtr<AActor> Instigator = nullptr;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
		uint8 Tier = 0;
	};
	TArray<FPendingDynConduction> PendingDynConductions;

	struct FCachedSurfaceProbe
	{
		bool bHit = false;
		EPhysicalMaterialType Material = EPhysicalMaterialType::Stone;
		TWeakObjectPtr<AActor> SurfaceActor = nullptr;
	};
	TMap<FSurfaceCellCoord, FCachedSurfaceProbe> StaticProbeCache;

	auto ResolveSurfaceAtCoord = [&](const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMat, AActor*& OutActor, TArray<EStatusEffectType>& OutStatuses) -> bool
	{
		OutMat = EPhysicalMaterialType::Stone;
		OutActor = nullptr;
		OutStatuses.Reset();

		if (const FSurfaceCellData* ExistingData = ActiveCells.Find(Coord))
		{
			if (!ExistingData->IsEmpty())
			{
				OutMat = ExistingData->SurfaceMaterial;
				OutActor = ExistingData->SurfaceActor.Get();
				OutStatuses = ExistingData->GetStatusTypes();
				return true;
			}
		}

		if (const FCachedSurfaceProbe* Cached = StaticProbeCache.Find(Coord))
		{
			if (Cached->bHit)
			{
				OutMat = Cached->Material;
				OutActor = Cached->SurfaceActor.Get();
				return true;
			}
			return false;
		}

		FHitResult Hit;
		EPhysicalMaterialType ProbedMat = EPhysicalMaterialType::Stone;
		const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
		const FVector Center = Coord.ToWorldLocation(SafeCellSize);
		const bool bHit = SurfaceGridGeometryUtils::ProbeSurfaceAt(World, Center, Normal, SafeCellSize * 0.8f, Hit, ProbedMat);

		FCachedSurfaceProbe NewProbe;
		NewProbe.bHit = bHit;
		if (bHit)
		{
			NewProbe.Material = ProbedMat;
			NewProbe.SurfaceActor = Hit.GetActor();
			OutMat = ProbedMat;
			OutActor = Hit.GetActor();
		}
		StaticProbeCache.Add(Coord, NewProbe);
		return bHit;
	};

	auto ShouldSkipVisitedStatic = [&](const FSurfaceCellCoord& Coord, float IncomingEndTime) -> bool
	{
		if (const float* VisitedTime = StaticVisitedEndTime.Find(Coord))
		{
			if (*VisitedTime == 0.0f) return true;
			if (IncomingEndTime > 0.0f && *VisitedTime >= IncomingEndTime - 0.05f) return true;
		}
		return false;
	};

	auto ShouldSkipExistingStatic = [&](const FSurfaceCellCoord& Coord, EStatusEffectType Status, float IncomingEndTime) -> bool
	{
		if (const FSurfaceCellData* ExistingData = ActiveCells.Find(Coord))
		{
			if (const FSurfaceCellStatusEntry* ExistingEntry = ExistingData->FindStatus(Status))
			{
				if (ExistingEntry->IsPermanent()) return true;
				if (IncomingEndTime > 0.0f && ExistingEntry->ServerEndTime >= IncomingEndTime - 0.05f) return true;
			}
		}
		return false;
	};

	auto ShouldSkipVisitedDynamic = [&](const TPair<TWeakObjectPtr<AActor>, FSurfaceCellCoord>& Key, float IncomingEndTime) -> bool
	{
		if (const float* VisitedTime = DynamicVisitedEndTime.Find(Key))
		{
			if (*VisitedTime == 0.0f) return true;
			if (IncomingEndTime > 0.0f && *VisitedTime >= IncomingEndTime - 0.05f) return true;
		}
		return false;
	};

	auto ShouldSkipExistingDynamic = [&](AActor* DynActor, const FSurfaceCellCoord& LocalCoord, EStatusEffectType Status, float IncomingEndTime) -> bool
	{
		if (const FDynamicActorSurfaceGrid* DynGrid = DynamicGrids.Find(DynActor))
		{
			if (const FSurfaceCellData* LocalData = DynGrid->LocalCells.Find(LocalCoord))
			{
				if (const FSurfaceCellStatusEntry* ExistingEntry = LocalData->FindStatus(Status))
				{
					if (ExistingEntry->IsPermanent()) return true;
					if (IncomingEndTime > 0.0f && ExistingEntry->ServerEndTime >= IncomingEndTime - 0.05f) return true;
				}
			}
		}
		return false;
	};

	const float MaxContactDistSq = FMath::Square(SafeCellSize * 1.5f);
	TArray<FSurfaceCellCoord> NeighborCoords;
	int32 HeadIndex = 0;

	while (HeadIndex < Queue.Num())
	{
		const FConductionItem CurrentItem = Queue[HeadIndex++];
		const float RemainingDuration = (CurrentItem.ServerEndTime == 0.0f) ? 0.0f : (CurrentItem.ServerEndTime - CurrentTime);
		if (CurrentItem.ServerEndTime != 0.0f && RemainingDuration <= 0.05f)
		{
			continue;
		}

		if (!CurrentItem.bIsDynamic)
		{
			// --- WĘZEŁ STATYCZNY ---
			const FSurfaceCellCoord& SourceCoord = CurrentItem.Coord;
			const FVector SourcePos = SourceCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(SourceCoord.Face) * (SafeCellSize * 0.45f);

			EPhysicalMaterialType SourceMat = EPhysicalMaterialType::Stone;
			AActor* SourceActor = nullptr;
			TArray<EStatusEffectType> SourceStatuses;
			ResolveSurfaceAtCoord(SourceCoord, SourceMat, SourceActor, SourceStatuses);

			auto ProcessCandidate = [&](const FSurfaceCellCoord& NeighborCoord, EPhysicalMaterialType NeighborMat, AActor* NeighborSurfaceActor, const TArray<EStatusEffectType>& NeighborActiveStatuses)
			{
				const bool bIsNeighborDynamic = SurfaceGridGeometryUtils::IsDynamicSurfaceTarget(NeighborSurfaceActor);

				if (bIsNeighborDynamic)
				{
					USceneComponent* TransformComp = FDynamicSurfaceGridManager::GetDynamicActorTransformComponent(NeighborSurfaceActor);
					if (!TransformComp)
					{
						return;
					}

					const FTransform RigidTransform = FDynamicSurfaceGridManager::GetDynamicRigidTransform(TransformComp);
					const FVector WorldCellPos = NeighborCoord.ToWorldLocation(SafeCellSize);
					const FVector LocalPos = RigidTransform.InverseTransformPosition(WorldCellPos);
					const FVector WorldNormal = SurfaceGridUtils::FaceDirectionToNormal(NeighborCoord.Face);
					const FVector LocalNorm = RigidTransform.InverseTransformVector(WorldNormal).GetSafeNormal();
					const FSurfaceCellCoord DynLocalCoord = FSurfaceCellCoord::FromWorldLocation(LocalPos, LocalNorm, SafeCellSize);

					// GLOBALNA ZASADA DYNAMIKI: Obiekt ruchomy przyjmuje przewodzenie ze świata WYŁĄCZNIE na komórki
					// już aktywne w swojej siatce lokalnej (LocalCells). Nigdy nie tworzy nowych komórek w próżni!
					const FDynamicActorSurfaceGrid* DynGrid = DynamicGrids.Find(NeighborSurfaceActor);
					if (!DynGrid)
					{
						return;
					}

					const FSurfaceCellData* LocalData = DynGrid->LocalCells.Find(DynLocalCoord);
					if (!LocalData || LocalData->IsEmpty())
					{
						return;
					}

					const EPhysicalMaterialType DynMat = LocalData->SurfaceMaterial;
					const TArray<EStatusEffectType> DynStatuses = LocalData->GetStatusTypes();

					if (!UElementalReactionRules::CanMaterialReceiveStatus(DynMat, CurrentItem.Status, DynStatuses))
					{
						return;
					}

					const TPair<TWeakObjectPtr<AActor>, FSurfaceCellCoord> DynKey(NeighborSurfaceActor, DynLocalCoord);
					if (ShouldSkipVisitedDynamic(DynKey, CurrentItem.ServerEndTime))
					{
						return;
					}
					if (ShouldSkipExistingDynamic(NeighborSurfaceActor, DynLocalCoord, CurrentItem.Status, CurrentItem.ServerEndTime))
					{
						return;
					}

					DynamicVisitedEndTime.Add(DynKey, CurrentItem.ServerEndTime);
					PendingDynConductions.Add({ NeighborSurfaceActor, TransformComp, DynLocalCoord, CurrentItem.Status, RemainingDuration, CurrentItem.Instigator, DynMat, CurrentItem.Tier });
					Queue.Add({ true, DynLocalCoord, NeighborSurfaceActor, TransformComp, CurrentItem.Status, CurrentItem.ServerEndTime, CurrentItem.Tier, CurrentItem.Instigator });
				}
				else
				{
					if (!UElementalReactionRules::CanMaterialReceiveStatus(NeighborMat, CurrentItem.Status, NeighborActiveStatuses))
					{
						return;
					}

					if (ShouldSkipVisitedStatic(NeighborCoord, CurrentItem.ServerEndTime))
					{
						return;
					}

					if (ShouldSkipExistingStatic(NeighborCoord, CurrentItem.Status, CurrentItem.ServerEndTime))
					{
						return;
					}

					StaticVisitedEndTime.Add(NeighborCoord, CurrentItem.ServerEndTime);
					PendingStaticConductions.Add(NeighborCoord, { NeighborCoord, CurrentItem.Status, RemainingDuration, CurrentItem.Instigator, NeighborMat, NeighborSurfaceActor, CurrentItem.Tier });
					Queue.Add({ false, NeighborCoord, nullptr, nullptr, CurrentItem.Status, CurrentItem.ServerEndTime, CurrentItem.Tier, CurrentItem.Instigator });
				}
			};

			// A. Sąsiedzi w siatce świata: ujednolicone wyszukiwanie topologiczne
			TArray<SurfaceGridGeometryUtils::FSurfaceSpreadCandidate> Candidates;
			SurfaceGridGeometryUtils::FindSpreadCandidates(World, SourceCoord, SourceActor, ActiveCells, SafeCellSize, Candidates);

			for (const auto& Candidate : Candidates)
			{
				ProcessCandidate(Candidate.Coord, Candidate.Material, Candidate.SurfaceActor, Candidate.ActiveStatuses);
			}

			// B. Sąsiedzi dynamiczni zarejestrowani w DynamicGrids (np. brama dotykająca posadzki/wody)
			const FVector NeighborWorldPos = SourceCoord.ToWorldLocation(SafeCellSize);
			const FVector NeighborWorldNorm = SurfaceGridUtils::FaceDirectionToNormal(SourceCoord.Face);

			for (const auto& GridPair : DynamicGrids)
			{
				AActor* DynActor = GridPair.Key.Get();
				USceneComponent* TransformComp = GridPair.Value.TransformComponent.Get();
				if (!DynActor || !TransformComp) continue;

				if (!TransformComp->Bounds.GetBox().ExpandBy(SafeCellSize * 1.5f).IsInsideOrOn(NeighborWorldPos)) continue;

				const FTransform RigidTransform = FDynamicSurfaceGridManager::GetDynamicRigidTransform(TransformComp);
				const FVector LocalPos = RigidTransform.InverseTransformPosition(NeighborWorldPos);
				const FVector LocalNorm = RigidTransform.InverseTransformVector(NeighborWorldNorm).GetSafeNormal();
				const FSurfaceCellCoord DynLocalCoord = FSurfaceCellCoord::FromWorldLocation(LocalPos, LocalNorm, SafeCellSize);

				// GLOBALNA ZASADA DYNAMIKI: Obiekt ruchomy przyjmuje przewodzenie ze świata WYŁĄCZNIE na komórki
				// już aktywne w swojej siatce lokalnej (LocalCells). Nigdy nie tworzy nowych komórek w próżni!
				const FSurfaceCellData* ExistingDynData = GridPair.Value.LocalCells.Find(DynLocalCoord);
				if (!ExistingDynData || ExistingDynData->IsEmpty())
				{
					continue;
				}

				const EPhysicalMaterialType DynMat = ExistingDynData->SurfaceMaterial;
				const TArray<EStatusEffectType> DynActiveStatuses = ExistingDynData->GetStatusTypes();

				if (!UElementalReactionRules::CanMaterialReceiveStatus(DynMat, CurrentItem.Status, DynActiveStatuses))
				{
					continue;
				}

				const FVector DynLocalPos = DynLocalCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(DynLocalCoord.Face) * (SafeCellSize * 0.45f);
				if (FVector::DistSquared(SourcePos, RigidTransform.TransformPosition(DynLocalPos)) > MaxContactDistSq)
				{
					continue;
				}

				const TPair<TWeakObjectPtr<AActor>, FSurfaceCellCoord> DynKey(DynActor, DynLocalCoord);
				if (ShouldSkipVisitedDynamic(DynKey, CurrentItem.ServerEndTime))
				{
					continue;
				}
				if (ShouldSkipExistingDynamic(DynActor, DynLocalCoord, CurrentItem.Status, CurrentItem.ServerEndTime))
				{
					continue;
				}

				DynamicVisitedEndTime.Add(DynKey, CurrentItem.ServerEndTime);
				PendingDynConductions.Add({ DynActor, TransformComp, DynLocalCoord, CurrentItem.Status, RemainingDuration, CurrentItem.Instigator, DynMat, CurrentItem.Tier });
				Queue.Add({ true, DynLocalCoord, DynActor, TransformComp, CurrentItem.Status, CurrentItem.ServerEndTime, CurrentItem.Tier, CurrentItem.Instigator });
			}
		}
		else
		{
			// --- WĘZEŁ DYNAMICZNY ---
			AActor* DynActor = CurrentItem.DynActor.Get();
			USceneComponent* TransformComp = CurrentItem.TransformComp.Get();
			if (!DynActor || !TransformComp) continue;

			const FTransform RigidTransform = FDynamicSurfaceGridManager::GetDynamicRigidTransform(TransformComp);
			const FVector LocalPos = CurrentItem.Coord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(CurrentItem.Coord.Face) * (SafeCellSize * 0.45f);
			const FVector WorldContactPos = RigidTransform.TransformPosition(LocalPos);
			const FVector WorldContactNorm = RigidTransform.TransformVector(SurfaceGridUtils::FaceDirectionToNormal(CurrentItem.Coord.Face)).GetSafeNormal();

			// A. Rozchodzenie się po komórkach lokalnych tego samego mesha (np. cała brama)
			// GLOBALNA ZASADA DYNAMIKI: Przewodzenie po meshu może przeskakiwać WYŁĄCZNIE na komórki
			// już istniejące w siatce lokalnej (np. wcześniej zwilżone cieczą). Nigdy nie tworzy nowych komórek w próżni!
			const FDynamicActorSurfaceGrid* DynGridPtr = DynamicGrids.Find(DynActor);
			if (DynGridPtr)
			{
				CurrentItem.Coord.GetCoplanarNeighbors(NeighborCoords);

				for (const FSurfaceCellCoord& LocalNeighborCoord : NeighborCoords)
				{
					if (LocalNeighborCoord.X == CurrentItem.Coord.X && LocalNeighborCoord.Y == CurrentItem.Coord.Y && LocalNeighborCoord.Z == CurrentItem.Coord.Z)
					{
						continue;
					}

					const FSurfaceCellData* ExistingLocalData = DynGridPtr->LocalCells.Find(LocalNeighborCoord);
					if (!ExistingLocalData || ExistingLocalData->IsEmpty())
					{
						continue;
					}

					const EPhysicalMaterialType DynMat = ExistingLocalData->SurfaceMaterial;
					const TArray<EStatusEffectType> DynLocalStatuses = ExistingLocalData->GetStatusTypes();

					if (!UElementalReactionRules::CanMaterialReceiveStatus(DynMat, CurrentItem.Status, DynLocalStatuses))
					{
						continue;
					}

					const TPair<TWeakObjectPtr<AActor>, FSurfaceCellCoord> DynKey(DynActor, LocalNeighborCoord);
					if (ShouldSkipVisitedDynamic(DynKey, CurrentItem.ServerEndTime))
					{
						continue;
					}
					if (ShouldSkipExistingDynamic(DynActor, LocalNeighborCoord, CurrentItem.Status, CurrentItem.ServerEndTime))
					{
						continue;
					}

					DynamicVisitedEndTime.Add(DynKey, CurrentItem.ServerEndTime);
					PendingDynConductions.Add({ DynActor, TransformComp, LocalNeighborCoord, CurrentItem.Status, RemainingDuration, CurrentItem.Instigator, DynMat, CurrentItem.Tier });
					Queue.Add({ true, LocalNeighborCoord, DynActor, TransformComp, CurrentItem.Status, CurrentItem.ServerEndTime, CurrentItem.Tier, CurrentItem.Instigator });
				}
			}

			// B. Dynamiczny obiekt dotykający architektury w świecie w punkcie kontaktu.
			// GLOBALNA ZASADA DYNAMIKI: Obiekt ruchomy wchodzi w interakcję WYŁĄCZNIE z istniejącymi już aktywnymi komórkami w świecie (ActiveCells).
			// Nigdy nie tworzy nowych komórek statycznych z powietrza / raycastów podczas ruchu!
			const FSurfaceCellCoord StaticWorldCoord = FSurfaceCellCoord::FromWorldLocation(WorldContactPos, WorldContactNorm, SafeCellSize);
			const FVector StaticNeighborPos = StaticWorldCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(StaticWorldCoord.Face) * (SafeCellSize * 0.45f);

			if (FVector::DistSquared(WorldContactPos, StaticNeighborPos) <= MaxContactDistSq)
			{
				if (const FSurfaceCellData* ExistingStaticData = ActiveCells.Find(StaticWorldCoord))
				{
					if (!ExistingStaticData->IsEmpty())
					{
						AActor* NeighborSurfaceActor = ExistingStaticData->SurfaceActor.Get();
						if (NeighborSurfaceActor != DynActor && !SurfaceGridGeometryUtils::IsDynamicSurfaceTarget(NeighborSurfaceActor))
						{
							const EPhysicalMaterialType NeighborMat = ExistingStaticData->SurfaceMaterial;
							const TArray<EStatusEffectType> NeighborActiveStatuses = ExistingStaticData->GetStatusTypes();

							if (UElementalReactionRules::CanMaterialReceiveStatus(NeighborMat, CurrentItem.Status, NeighborActiveStatuses))
							{
								if (!ShouldSkipVisitedStatic(StaticWorldCoord, CurrentItem.ServerEndTime) &&
									!ShouldSkipExistingStatic(StaticWorldCoord, CurrentItem.Status, CurrentItem.ServerEndTime))
								{
									StaticVisitedEndTime.Add(StaticWorldCoord, CurrentItem.ServerEndTime);
									PendingStaticConductions.Add(StaticWorldCoord, { StaticWorldCoord, CurrentItem.Status, RemainingDuration, CurrentItem.Instigator, NeighborMat, NeighborSurfaceActor, CurrentItem.Tier });
									Queue.Add({ false, StaticWorldCoord, nullptr, nullptr, CurrentItem.Status, CurrentItem.ServerEndTime, CurrentItem.Tier, CurrentItem.Instigator });
								}
							}
						}
					}
				}
			}
		}
	}

	// 2. Aplikujemy zakolejkowane komórki dynamiczne
	for (const FPendingDynConduction& Conduction : PendingDynConductions)
	{
		if (Conduction.DynActor.IsValid() && Conduction.TransformComp.IsValid())
		{
			DynamicGrid.ApplyStatusToDynamicCell(
				Conduction.DynActor.Get(),
				Conduction.TransformComp.Get(),
				Conduction.LocalCoord,
				Conduction.Status,
				Conduction.Duration,
				Conduction.Instigator.Get(),
				Conduction.Material,
				Conduction.Tier,
				SafeCellSize,
				CurrentTime,
				OnCellChanged);
		}
	}

	// 3. Aplikujemy zakolejkowane komórki statyczne
	for (const auto& Pair : PendingStaticConductions)
	{
		const FPendingStaticConduction& Conduction = Pair.Value;
		StaticGrid.ApplyStatusToCell(
			World,
			Conduction.Coord,
			Conduction.Status,
			Conduction.Duration,
			Conduction.Instigator.Get(),
			Conduction.Material,
			Conduction.SurfaceActor.Get(),
			Conduction.Tier,
			SafeCellSize,
			CurrentTime,
			OnCellChanged);
	}
}

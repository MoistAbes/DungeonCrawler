#include "SurfaceCellTransitionUtils.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Logging/DungeonLogCategories.h"

namespace SurfaceCellTransitionUtils_Private
{
	/** Aktualizuje czas istniejącego wpisu w komórce lub dodaje nowy wpis (Upsert) */
	void UpsertStatusEntry(FSurfaceCellData& CellData, EStatusEffectType Status, float EndTime, AActor* Instigator, uint8 Tier = 0)
	{
		const bool bCanBePermanent = UElementalReactionRules::IsPermanentStatus(CellData.SurfaceMaterial, Status);

		if (FSurfaceCellStatusEntry* Existing = CellData.FindStatus(Status))
		{
			if (bCanBePermanent && (Existing->IsPermanent() || EndTime == 0.0f))
			{
				Existing->SetPermanent();
			}
			else if (!Existing->IsPermanent())
			{
				Existing->ServerEndTime = FMath::Max(Existing->ServerEndTime, EndTime);
			}

			Existing->Tier = FMath::Max(Existing->Tier, Tier);

			if (Instigator)
			{
				Existing->Instigator = Instigator;
			}
		}
		else
		{
			const float SafeEndTime = (bCanBePermanent && EndTime == 0.0f) ? 0.0f : EndTime;
			CellData.ActiveStatuses.Emplace(Status, SafeEndTime, Instigator, Tier);
		}
	}

	/** Synchronizuje (przedłuża) czasy trwania statusów zależnych, gdy dodano lub odświeżono nośnik */
	void SyncDependentsWithCarrier(FSurfaceCellData& CellData, EStatusEffectType CarrierStatus, float CarrierEndTime)
	{
		for (FSurfaceCellStatusEntry& OtherEntry : CellData.ActiveStatuses)
		{
			if (OtherEntry.Status != CarrierStatus && UElementalReactionRules::DoesStatusSyncWithCarrier(OtherEntry.Status, CarrierStatus))
			{
				const bool bDependentCanBePermanent = UElementalReactionRules::IsPermanentStatus(CellData.SurfaceMaterial, OtherEntry.Status);
				if (CarrierEndTime == 0.0f && bDependentCanBePermanent)
				{
					OtherEntry.SetPermanent();
				}
				else if (CarrierEndTime > 0.0f && !OtherEntry.IsPermanent())
				{
					OtherEntry.ServerEndTime = FMath::Max(OtherEntry.ServerEndTime, CarrierEndTime);
				}
			}
		}
	}
}

bool USurfaceCellTransitionUtils::CleanOrphanedStatuses(FSurfaceCellData& InOutCellData, EStatusEffectType StatusToPreserve)
{
	bool bEvictedAny = false;
	const TArray<EStatusEffectType> ActiveBefore = InOutCellData.GetStatusTypes();
	for (int32 Index = InOutCellData.ActiveStatuses.Num() - 1; Index >= 0; --Index)
	{
		const EStatusEffectType RemainingStatus = InOutCellData.ActiveStatuses[Index].Status;
		if (RemainingStatus == StatusToPreserve)
		{
			continue;
		}

		if (!UElementalReactionRules::CanMaterialReceiveStatus(InOutCellData.SurfaceMaterial, RemainingStatus, ActiveBefore))
		{
			// UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceCellTransitionUtils] CleanOrphanedStatuses: Evicted orphaned status %s on material %s (carrier expired)"),
			// 	*UEnum::GetValueAsString(RemainingStatus), *UEnum::GetValueAsString(InOutCellData.SurfaceMaterial));
			InOutCellData.ActiveStatuses.RemoveAt(Index);
			bEvictedAny = true;
		}
	}
	return bEvictedAny;
}

FSurfaceCellTransitionResult USurfaceCellTransitionUtils::CalculateCellTransition(
	FSurfaceCellData& InOutCellData,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	float CurrentTime,
	uint8 Tier)
{
	FSurfaceCellTransitionResult Result;

	if (IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return Result;
	}

	// 1. Zbuduj migawkę aktywnych statusów komórki
	TArray<FElementalActiveStatusSnapshot> Snapshots;
	for (const FSurfaceCellStatusEntry& Entry : InOutCellData.ActiveStatuses)
	{
		Snapshots.Add({
			Entry.Status,
			Entry.Tier,
			Entry.IsPermanent() ? 0.0f : Entry.GetRemainingDuration(CurrentTime),
			Entry.IsPermanent()
		});
	}

	// 2. Wywołaj centralny kalkulator przejścia (Single Source of Truth)
	const FElementalTransitionPlan Plan = UElementalReactionRules::CalculateElementalTransition(
		InOutCellData.SurfaceMaterial,
		IncomingStatus,
		Duration,
		Tier,
		Snapshots);

	Result.bAccepted = Plan.bAccepted;
	Result.bStateModified = Plan.bStateModified;
	Result.bCellBecameEmpty = Plan.bBecameEmpty;
	Result.Reaction = Plan.PrimaryReaction;

	if (!Plan.bAccepted)
	{
		return Result;
	}

	// 3. Zaaplikuj usunięcia statusów
	for (EStatusEffectType StatusToRemove : Plan.StatusesToRemove)
	{
		InOutCellData.RemoveStatus(StatusToRemove);
	}

	// 4. Zaaplikuj nałożenia / aktualizacje statusów
	for (const FElementalStatusApplyInfo& ApplyInfo : Plan.StatusesToApply)
	{
		const float EndTime = ApplyInfo.bPermanent ? 0.0f : (CurrentTime + ApplyInfo.Duration);
		SurfaceCellTransitionUtils_Private::UpsertStatusEntry(InOutCellData, ApplyInfo.Status, EndTime, Instigator, ApplyInfo.Tier);
		if (ApplyInfo.bSyncWithCarrierDuration || UElementalReactionRules::IsLiquidStatus(ApplyInfo.Status))
		{
			SurfaceCellTransitionUtils_Private::SyncDependentsWithCarrier(InOutCellData, ApplyInfo.Status, EndTime);
		}
	}

	Result.bCellBecameEmpty = InOutCellData.IsEmpty();
	return Result;
}

bool USurfaceCellTransitionUtils::ApplyStatusToCellInMap(
	TMap<FSurfaceCellCoord, FSurfaceCellData>& CellMap,
	const FSurfaceCellCoord& Coord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial,
	AActor* SurfaceActor,
	uint8 Tier,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged,
	const FVector& SurfaceLocation)
{
	if (IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	FSurfaceCellData* Existing = CellMap.Find(Coord);
	FSurfaceCellData CellData;
	if (Existing)
	{
		CellData = *Existing;
		if (CellData.SurfaceLocation.IsNearlyZero() && !SurfaceLocation.IsNearlyZero())
		{
			CellData.SurfaceLocation = SurfaceLocation;
		}
	}
	else
	{
		CellData.SurfaceMaterial = ExplicitMaterial;
		CellData.SurfaceActor = SurfaceActor;
		CellData.SurfaceLocation = SurfaceLocation;
	}

	const FSurfaceCellTransitionResult Result = CalculateCellTransition(
		CellData,
		IncomingStatus,
		Duration,
		Instigator,
		CurrentTime,
		Tier);

	if (!Result.bAccepted)
	{
		return false;
	}

	// Inicjalizacja czasu pierwszego rozprzestrzenienia dla stałego paliwa (np. drewno)
	if (CellData.HasStatus(EStatusEffectType::Burning))
	{
		const FPhysicalMaterialTraits Traits = PhysicalMaterialUtils::GetTraits(CellData.SurfaceMaterial);
		if (Traits.bSelfSustainingFuel && CellData.NextFuelSpreadTime <= 0.0f)
		{
			CellData.NextFuelSpreadTime = CurrentTime + Traits.FuelSpreadInterval;
		}
	}

	// Komórka została opróżniona (np. ugaszenie ognia wodą)
	if (Result.bCellBecameEmpty)
	{
		CellMap.Remove(Coord);
		OnCellChanged(Coord, EStatusEffectType::None, Instigator);
		return true;
	}

	// Zapisanie nowego stanu komórki
	CellMap.Add(Coord, CellData);
	OnCellChanged(Coord, CellData.GetDominantStatus(), CellData.GetDominantInstigator());

	return true;
}

void USurfaceCellTransitionUtils::ExpireCellsInMap(
	TMap<FSurfaceCellCoord, FSurfaceCellData>& CellMap,
	float CurrentTime,
	TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged)
{
	for (auto It = CellMap.CreateIterator(); It; ++It)
	{
		FSurfaceCellData& Cell = It.Value();
		bool bStatusRemoved = false;

		for (int32 Index = Cell.ActiveStatuses.Num() - 1; Index >= 0; --Index)
		{
			if (Cell.ActiveStatuses[Index].IsExpired(CurrentTime))
			{
				Cell.ActiveStatuses.RemoveAt(Index);
				bStatusRemoved = true;
			}
		}

		if (bStatusRemoved && !Cell.IsEmpty())
		{
			CleanOrphanedStatuses(Cell);
		}

		if (Cell.IsEmpty())
		{
			OnCellChanged(It.Key(), EStatusEffectType::None, nullptr);
			It.RemoveCurrent();
		}
		else if (bStatusRemoved)
		{
			OnCellChanged(It.Key(), Cell.GetDominantStatus(), Cell.GetDominantInstigator());
		}
	}
}

FColor USurfaceCellTransitionUtils::GetCellDebugColor(const FSurfaceCellData& CellData)
{
	if (CellData.IsEmpty())
	{
		return FColor(200, 200, 200);
	}

	if (CellData.HasStatus(EStatusEffectType::Wet) && CellData.HasStatus(EStatusEffectType::Electrified))
	{
		return FColor(0, 255, 255); // Cyan / Electric Blue
	}

	switch (CellData.GetDominantStatus())
	{
	case EStatusEffectType::Burning:     return FColor(255, 69, 0);   // Red-Orange
	case EStatusEffectType::Wet:         return FColor(30, 144, 255); // Dodger Blue
	case EStatusEffectType::Oiled:       return FColor(139, 69, 19);  // Saddle Brown
	case EStatusEffectType::Electrified: return FColor(255, 215, 0);  // Gold
	default:                             return FColor(200, 200, 200);
	}
}

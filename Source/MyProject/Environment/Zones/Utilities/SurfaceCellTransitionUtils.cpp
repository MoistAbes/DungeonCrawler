#include "SurfaceCellTransitionUtils.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Logging/DungeonLogCategories.h"

namespace
{
	FORCEINLINE bool IsPermanentEffect(EPhysicalMaterialType Material, EStatusEffectType Status)
	{
		return (Status == EStatusEffectType::Burning) && PhysicalMaterialUtils::GetTraits(Material).bSelfSustainingFuel;
	}

	/** Aktualizuje czas istniejącego wpisu w komórce lub dodaje nowy wpis (Upsert) */
	void UpsertStatusEntry(FSurfaceCellData& CellData, EStatusEffectType Status, float EndTime, AActor* Instigator, uint8 Tier = 0)
	{
		const bool bCanBePermanent = IsPermanentEffect(CellData.SurfaceMaterial, Status);

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
				const bool bDependentCanBePermanent = IsPermanentEffect(CellData.SurfaceMaterial, OtherEntry.Status);
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
			UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceCellTransitionUtils] CleanOrphanedStatuses: Evicted orphaned status %s on material %s (carrier expired)"),
				*UEnum::GetValueAsString(RemainingStatus), *UEnum::GetValueAsString(InOutCellData.SurfaceMaterial));
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
		UpsertStatusEntry(InOutCellData, ApplyInfo.Status, EndTime, Instigator, ApplyInfo.Tier);
		if (ApplyInfo.bSyncWithCarrierDuration || UElementalReactionRules::IsLiquidStatus(ApplyInfo.Status))
		{
			SyncDependentsWithCarrier(InOutCellData, ApplyInfo.Status, EndTime);
		}
	}

	Result.bCellBecameEmpty = InOutCellData.IsEmpty();
	return Result;
}

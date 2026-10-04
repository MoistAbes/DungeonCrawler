 #include "SurfaceActorInteractionUtils.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "GameFramework/Actor.h"

void SurfaceActorInteractionUtils::ApplyActorEffectsToFloor(
	AActor* Actor,
	UStatusEffectComponent* StatusComp,
	const TArray<FSurfaceCellCoord>& TouchedCells,
	TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
	TArray<EStatusEffectType>& InOutActorStatuses,
	FApplyStatusToCellFunc ApplyStatusToCell)
{
	if (!Actor || !StatusComp)
	{
		return;
	}

	for (const FSurfaceCellCoord& CellCoord : TouchedCells)
	{
		FSurfaceCellData* CellData = ActiveCells.Find(CellCoord);
		if (!CellData || CellData->IsEmpty())
		{
			continue;
		}

		// Interakcja Obiekt -> Komórka (z zachowaniem priorytetu reakcji i braku fałszywego break)
		for (int32 StatusIdx = 0; StatusIdx < InOutActorStatuses.Num(); ++StatusIdx)
		{
			const EStatusEffectType ActorStatus = InOutActorStatuses[StatusIdx];
			if (ActorStatus == EStatusEffectType::None || !UElementalReactionRules::CanStatusTransferToFloor(ActorStatus))
			{
				continue;
			}

			// Czy komórka nadal istnieje i ma z czym reagować?
			if (!CellData || CellData->IsEmpty())
			{
				break;
			}

			const FElementalReactionResult Reaction = UElementalReactionRules::EvaluateReaction(ActorStatus, CellData->GetStatusTypes());
			if (Reaction.bReactionOccurred)
			{
				// Obiekt swoją obecnością NIE wypiera cieczy na posadzce
				if (Reaction.ReactionTag == ElementalReactionTags::LiquidDisplaced())
				{
					continue;
				}

				// Bezpiecznik fizyczny: jeśli komórka już posiada status docelowy
				const EStatusEffectType TargetStatus = (Reaction.ResultingStatus != EStatusEffectType::None) ? Reaction.ResultingStatus : ActorStatus;
				if (CellData->HasStatus(TargetStatus))
				{
					continue;
				}

				// Jeśli status obiektu uległ zużyciu w reakcji
				if (Reaction.bConsumeIncomingStatus)
				{
					StatusComp->RemoveStatus(ActorStatus);
					InOutActorStatuses.RemoveAt(StatusIdx);
					--StatusIdx;
				}

				const float FallbackDuration = UElementalReactionRules::GetEffectConfig(TargetStatus).GetBaseDuration();
				const float ReactionDuration = (Reaction.ResultingDuration > 0.0f ? Reaction.ResultingDuration : FallbackDuration);
				ApplyStatusToCell(CellCoord, TargetStatus, ReactionDuration, Actor);

				// Po ewentualnej modyfikacji komórki przez ApplyStatusToCell odświeżamy wskaźnik
				CellData = ActiveCells.Find(CellCoord);
			}
		}
	}
}

void SurfaceActorInteractionUtils::ApplyFloorEffectsToActor(
	AActor* Actor,
	UStatusEffectComponent* StatusComp,
	const TArray<FSurfaceCellCoord>& TouchedCells,
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
	float CellSize)
{
	if (!Actor || !StatusComp)
	{
		return;
	}

	struct FFloorStatusCandidate
	{
		uint8 Tier = 0;
		TWeakObjectPtr<AActor> Instigator = nullptr;
	};

	TMap<EStatusEffectType, FFloorStatusCandidate> StatusCandidates;
	EStatusEffectType DominantLiquid = EStatusEffectType::None;
	uint8 DominantLiquidTier = 0;
	TWeakObjectPtr<AActor> DominantLiquidInstigator = nullptr;
	float MinLiquidDistSq = TNumericLimits<float>::Max();
	const FVector ActorLocation = Actor->GetActorLocation();
	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	for (const FSurfaceCellCoord& CellCoord : TouchedCells)
	{
		const FSurfaceCellData* CellData = ActiveCells.Find(CellCoord);
		if (!CellData || CellData->IsEmpty())
		{
			continue;
		}

		const float CellDistSq = FVector::DistSquared(CellCoord.ToWorldLocation(SafeCellSize), ActorLocation);

		for (const FSurfaceCellStatusEntry& Entry : CellData->ActiveStatuses)
		{
			// Geometryczne rozstrzygnięcie: Na styku wielu komórek postać przyjmuje płyn z najbliższej komórki
			if (UElementalReactionRules::IsLiquidStatus(Entry.Status))
			{
				if (CellDistSq < MinLiquidDistSq)
				{
					MinLiquidDistSq = CellDistSq;
					DominantLiquid = Entry.Status;
					DominantLiquidTier = Entry.Tier;
					DominantLiquidInstigator = Entry.Instigator;
				}
			}
			else
			{
				FFloorStatusCandidate& Candidate = StatusCandidates.FindOrAdd(Entry.Status);
				if (Entry.Tier > Candidate.Tier || !Candidate.Instigator.IsValid())
				{
					Candidate.Tier = Entry.Tier;
					Candidate.Instigator = Entry.Instigator;
				}
			}
		}
	}

	// Dołączamy dominujący płyn wyłoniony na podstawie bliskości geometrycznej
	if (DominantLiquid != EStatusEffectType::None)
	{
		FFloorStatusCandidate& LiquidCandidate = StatusCandidates.FindOrAdd(DominantLiquid);
		LiquidCandidate.Tier = DominantLiquidTier;
		LiquidCandidate.Instigator = DominantLiquidInstigator;
	}

	if (StatusCandidates.IsEmpty())
	{
		return;
	}

	// Pobieramy listę statusów i sortujemy według reguł wnikania żywiołów (Single Source of Truth):
	// Nośniki/płyny są gwarantowane na początku (by przygotować powłokę materiału),
	// a następnie energie w kolejności priorytetu reakcji.
	TArray<EStatusEffectType> SortedStatuses;
	StatusCandidates.GetKeys(SortedStatuses);
	UElementalReactionRules::SortByIngressPriority(SortedStatuses);

	// Deterministyczna aplikacja statusów w kolejności podyktowanej przez silnik chemii
	for (EStatusEffectType StatusToApply : SortedStatuses)
	{
		if (const FFloorStatusCandidate* Candidate = StatusCandidates.Find(StatusToApply))
		{
			StatusComp->ApplyStatus(StatusToApply, Candidate->Tier, -1.0f, Candidate->Instigator.Get());

			if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
			{
				return;
			}
		}
	}
}

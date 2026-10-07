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

				const float ActorRemaining = StatusComp->GetRemainingDuration(ActorStatus);
				const float ConfigBaseDuration = UElementalReactionRules::GetEffectConfig(TargetStatus).GetBaseDuration();
				const float FallbackDuration = (ActorRemaining > 0.0f) ? ActorRemaining : ConfigBaseDuration;
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
	float CellSize,
	const FTransform* WorldTransform)
{
	if (!Actor || !StatusComp)
	{
		return;
	}

	struct FFloorStatusCandidate
	{
		uint8 Tier = 0;
		float MaxRemainingDuration = 0.0f;
		bool bPermanent = false;
		TWeakObjectPtr<AActor> Instigator = nullptr;
	};

	TMap<EStatusEffectType, FFloorStatusCandidate> StatusCandidates;
	EStatusEffectType DominantLiquid = EStatusEffectType::None;
	uint8 DominantLiquidTier = 0;
	float DominantLiquidRemaining = 0.0f;
	bool bDominantLiquidPermanent = false;
	TWeakObjectPtr<AActor> DominantLiquidInstigator = nullptr;
	float MinLiquidDistSq = TNumericLimits<float>::Max();
	const FVector ActorLocation = Actor->GetActorLocation();
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = Actor->GetWorld() ? Actor->GetWorld()->GetTimeSeconds() : 0.0f;

	for (const FSurfaceCellCoord& CellCoord : TouchedCells)
	{
		const FSurfaceCellData* CellData = ActiveCells.Find(CellCoord);
		if (!CellData || CellData->IsEmpty())
		{
			continue;
		}

		const FVector CellBasePos = SurfaceGridUtils::GetSurfaceBaseLocation(CellCoord, CellData->SurfaceLocation, SafeCellSize);
		const FVector CellWorldPos = WorldTransform ? WorldTransform->TransformPosition(CellBasePos) : CellBasePos;
		const float CellDistSq = FVector::DistSquared(CellWorldPos, ActorLocation);

		for (const FSurfaceCellStatusEntry& Entry : CellData->ActiveStatuses)
		{
			const bool bEntryPerm = Entry.IsPermanent();
			const float EntryRemaining = bEntryPerm ? 0.0f : Entry.GetRemainingDuration(CurrentTime);
			if (!bEntryPerm && EntryRemaining <= 0.05f)
			{
				continue;
			}

			// Geometryczne rozstrzygnięcie: Na styku wielu komórek postać przyjmuje płyn z najbliższej komórki
			if (UElementalReactionRules::IsLiquidStatus(Entry.Status))
			{
				if (CellDistSq < MinLiquidDistSq)
				{
					MinLiquidDistSq = CellDistSq;
					DominantLiquid = Entry.Status;
					DominantLiquidTier = Entry.Tier;
					DominantLiquidInstigator = Entry.Instigator;
					DominantLiquidRemaining = EntryRemaining;
					bDominantLiquidPermanent = bEntryPerm;
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
				if (bEntryPerm)
				{
					Candidate.bPermanent = true;
				}
				else
				{
					Candidate.MaxRemainingDuration = FMath::Max(Candidate.MaxRemainingDuration, EntryRemaining);
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
		LiquidCandidate.bPermanent = bDominantLiquidPermanent;
		LiquidCandidate.MaxRemainingDuration = DominantLiquidRemaining;
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
			const float ActorCurrentRemaining = StatusComp->GetRemainingDuration(StatusToApply);

			// Zasada zachowania energii: Jeśli postać już posiada ten status i ma więcej czasu niż źródło pod stopami,
			// nie odświeżamy sztucznie czasu do pełnego maksimum (zapobiega to wiecznym pętlom odświeżania).
			if (ActorCurrentRemaining > 0.0f && !Candidate->bPermanent && ActorCurrentRemaining >= Candidate->MaxRemainingDuration - 0.05f)
			{
				continue;
			}

			const float DurationToApply = Candidate->bPermanent ? -1.0f : Candidate->MaxRemainingDuration;
			StatusComp->ApplyStatus(StatusToApply, Candidate->Tier, DurationToApply, Candidate->Instigator.Get());

			if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
			{
				return;
			}
		}
	}
}

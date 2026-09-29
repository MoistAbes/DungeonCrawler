#include "DungeonSurfaceSubsystem.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceCellTransitionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridProjectionUtils.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceActorInteractionUtils.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Environment/Zones/Utilities/SurfaceGridGeometryUtils.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Logging/DungeonLogCategories.h"

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

UDungeonSurfaceSubsystem::UDungeonSurfaceSubsystem()
{
	CellSize = 50.0f;
	SubsystemTickInterval = 0.25f;
	bDrawDebugGrid = true;
}

bool UDungeonSurfaceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	// Tworzymy podsystem wyłącznie dla właściwych światów gry (Game, PIE, Dedicated Server, Listen Server)
	return World->IsGameWorld();
}

void UDungeonSurfaceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			GridTickTimerHandle,
			this,
			&UDungeonSurfaceSubsystem::ProcessGridTick,
			SubsystemTickInterval,
			true);
	}

	UE_LOG(LogDungeonElements, Log, TEXT("[DungeonSurfaceSubsystem] Initialized with CellSize: %.1f cm, TickInterval: %.2fs"),
		CellSize, SubsystemTickInterval);
}

void UDungeonSurfaceSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GridTickTimerHandle);
	}

	ActiveCells.Empty();
	Super::Deinitialize();
}

bool UDungeonSurfaceSubsystem::IsValidSurfaceTarget(const AActor* Actor)
{
	return SurfaceGridGeometryUtils::IsValidSurfaceTarget(Actor);
}

int32 UDungeonSurfaceSubsystem::ApplyStatusFromHit(
	const FHitResult& HitResult,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !HitResult.bBlockingHit || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	AActor* HitActor = HitResult.GetActor();

	// Komórki powierzchniowe mogą powstać WYŁĄCZNIE na fundamentach lochu lub geometrii poziomu.
	// Żadnego proxy przez postacie czy rekwizyty posiadające własne komponenty statusów.
	if (!IsValidSurfaceTarget(HitActor))
	{
		return 0;
	}

	// Wyliczenie orientacji powłoki powierzchniowej i nałożenie statusu na komórki
	const FVector SurfaceNormal = HitResult.ImpactNormal.IsNearlyZero() ? FVector::UpVector : HitResult.ImpactNormal.GetSafeNormal();

	return ApplyStatusToSurface(
		HitResult.ImpactPoint,
		SurfaceNormal,
		Radius,
		Status,
		Duration,
		Instigator,
		Tier);
}

bool UDungeonSurfaceSubsystem::ApplyStatusToCell(
	const FSurfaceCellCoord& Coord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial,
	AActor* SurfaceActor,
	uint8 Tier)
{
	if (!GetWorld() || IncomingStatus == EStatusEffectType::None || Duration < 0.0f)
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	FSurfaceCellData* Existing = ActiveCells.Find(Coord);

	// Jeśli komórka nie istnieje w siatce, ustalamy tożsamość materiałową podłoża
	// oraz upewniamy się, że pod komórką faktycznie istnieje fizyczna architektura lochu.
	EPhysicalMaterialType SurfaceMat = ExplicitMaterial;
	AActor* ResolvedSurfaceActor = SurfaceActor;

	if (!Existing)
	{
		AActor* ProbedActor = nullptr;
		if (!GetSurfaceMaterialAtCoord(Coord, SurfaceMat, ProbedActor))
		{
			UE_LOG(LogDungeonElements, Verbose, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) rejected: No valid surface geometry present!"),
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

	FSurfaceCellData CellData;
	if (Existing)
	{
		CellData = *Existing;
	}
	else
	{
		CellData.SurfaceMaterial = SurfaceMat;
	}

	if (ResolvedSurfaceActor)
	{
		CellData.SurfaceActor = ResolvedSurfaceActor;
	}

	// Cała chemia, reakcje, nośniki i wygaszanie są liczone w centralnym silniku zasad (UElementalReactionRules).
	// Komórka jedynie odbiera i utrwala nowy stan za pośrednictwem narzędzia domenowego USurfaceCellTransitionUtils.
	const FSurfaceCellTransitionResult Result = USurfaceCellTransitionUtils::CalculateCellTransition(
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

	// Inicjalizacja czasu rozprzestrzeniania dla stałego paliwa (np. drewno)
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
		UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) became EMPTY after incoming %s"),
			Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face), *UEnum::GetValueAsString(IncomingStatus));
		ActiveCells.Remove(Coord);
		OnSurfaceCellChanged.Broadcast(Coord, EStatusEffectType::None, Instigator);
		return true;
	}

	// Zapisanie nowego stanu komórki
	ActiveCells.Add(Coord, CellData);

	if (Result.bStateModified)
	{
		FString StatusesStr;
		for (const auto& St : CellData.ActiveStatuses)
		{
			if (St.IsPermanent())
			{
				StatusesStr += FString::Printf(TEXT("[%s (T%d): Permanent] "), *UEnum::GetValueAsString(St.Status), St.Tier);
			}
			else
			{
				StatusesStr += FString::Printf(TEXT("[%s (T%d): %.1fs] "), *UEnum::GetValueAsString(St.Status), St.Tier, St.GetRemainingDuration(CurrentTime));
			}
		}
		UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) Updated -> %s"),
			Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face), *StatusesStr);
		OnSurfaceCellChanged.Broadcast(Coord, CellData.GetDominantStatus(), CellData.GetDominantInstigator());
	}

	return true;
}

int32 UDungeonSurfaceSubsystem::ApplyStatusToSurface(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	if (!GetWorld() || Status == EStatusEffectType::None || Radius <= 0.0f || Duration <= 0.0f)
	{
		return 0;
	}

	int32 AffectedCount = 0;
	SurfaceGridProjectionUtils::ProjectStatusToSurface(
		GetWorld(),
		HitLocation,
		HitNormal,
		Radius,
		CellSize,
		Instigator,
		[&](const FSurfaceCellCoord& Coord, EPhysicalMaterialType HitMat, AActor* SurfaceActor)
		{
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator, HitMat, SurfaceActor, Tier))
			{
				AffectedCount++;
			}
		});

	return AffectedCount;
}

int32 UDungeonSurfaceSubsystem::ApplyStatusInArea(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	const FVector& BurstOrigin,
	TSet<FSurfaceCellCoord>& ProcessedCoords,
	AActor* Instigator,
	uint8 Tier)
{
	if (!GetWorld() || Status == EStatusEffectType::None || Radius <= 0.0f || Duration <= 0.0f)
	{
		return 0;
	}

	int32 AffectedCount = 0;
	SurfaceGridProjectionUtils::ProjectStatusInArea(
		GetWorld(),
		HitLocation,
		HitNormal,
		Radius,
		BurstOrigin,
		CellSize,
		ProcessedCoords,
		Instigator,
		[&](const FSurfaceCellCoord& Coord, EPhysicalMaterialType HitMat, AActor* SurfaceActor)
		{
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator, HitMat, SurfaceActor, Tier))
			{
				AffectedCount++;
			}
		});

	return AffectedCount;
}

int32 UDungeonSurfaceSubsystem::ClearCellsInBounds(const FBox& BoundingBox)
{
	int32 RemovedCount = 0;
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const FBox ExpandedBox = BoundingBox.ExpandBy(SafeCellSize * 0.5f);

	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		const FVector Center = It.Key().ToWorldLocation(SafeCellSize);
		if (ExpandedBox.IsInsideOrOn(Center))
		{
			// Upewniamy się, że pod komórką faktycznie nie ma już geometrii (np. nie usuwamy nienaruszonej podłogi pod zniszczoną ścianą)
			EPhysicalMaterialType SurvivingMat;
			if (GetSurfaceMaterialAtCoord(It.Key(), SurvivingMat))
			{
				continue;
			}

			OnSurfaceCellChanged.Broadcast(It.Key(), EStatusEffectType::None, nullptr);
			It.RemoveCurrent();
			RemovedCount++;
		}
	}

	if (RemovedCount > 0)
	{
		UE_LOG(LogDungeonElements, Log, TEXT("[DungeonSurfaceSubsystem] Cleared %d cells in destroyed structure bounds"), RemovedCount);
	}

	return RemovedCount;
}

void UDungeonSurfaceSubsystem::RegisterStatusComponent(UStatusEffectComponent* Comp)
{
	if (Comp)
	{
		RegisteredStatusComponents.AddUnique(Comp);
	}
}

void UDungeonSurfaceSubsystem::UnregisterStatusComponent(UStatusEffectComponent* Comp)
{
	RegisteredStatusComponents.Remove(Comp);
}

bool UDungeonSurfaceSubsystem::GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial) const
{
	AActor* DummyActor = nullptr;
	return GetSurfaceMaterialAtCoord(Coord, OutMaterial, DummyActor);
}

bool UDungeonSurfaceSubsystem::GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial, AActor*& OutSurfaceActor) const
{
	OutSurfaceActor = nullptr;
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
	const FVector Center = Coord.ToWorldLocation(SafeCellSize);
	FHitResult Hit;
	const bool bHit = SurfaceGridGeometryUtils::ProbeSurfaceAt(GetWorld(), Center, Normal, SafeCellSize * 0.8f, Hit, OutMaterial);
	if (bHit)
	{
		OutSurfaceActor = Hit.GetActor();
	}
	return bHit;
}

int32 UDungeonSurfaceSubsystem::ApplyElementalBurst(
	const FVector& Origin,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	uint8 Tier)
{
	if (!GetWorld() || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyElementalBurst START -> Origin: %s, Radius: %.1f, Status: %s (Tier: %d)"),
		*Origin.ToString(), Radius, *UEnum::GetValueAsString(Status), Tier);

	int32 AffectedCount = 0;

	// 1. Zbiór komórek przetworzonych w ramach tego wybuchu (brak wyścigów i podwójnego malowania)
	TSet<FSurfaceCellCoord> ProcessedCoords;

	// 2. Bezpośrednia ewaluacja istniejących aktywnych komórek w sferze wybuchu z Line-of-Sight
	TArray<FSurfaceCellCoord> CellsInRadius;
	SurfaceGridProjectionUtils::FilterCellsInBurstRadius(
		GetWorld(),
		Origin,
		Radius,
		CellSize,
		ActiveCells,
		Instigator,
		CellsInRadius);

	for (const FSurfaceCellCoord& Coord : CellsInRadius)
	{
		// Bezpiecznik: jeśli komórka została usunięta z ActiveCells w trakcie tej samej pętli
		if (!ActiveCells.Contains(Coord))
		{
			continue;
		}

		ProcessedCoords.Add(Coord);
		if (ApplyStatusToCell(Coord, Status, Duration, Instigator, EPhysicalMaterialType::Stone, nullptr, Tier))
		{
			AffectedCount++;
		}
	}

	// 3. Wszechkierunkowa projekcja wybuchu na otaczające powierzchnie lochu (posadzka, sufit, ściany, rampy)
	SurfaceGridProjectionUtils::ScanBurstSurfaces(
		GetWorld(),
		Origin,
		Radius,
		CellSize,
		Instigator,
		[&](const FHitResult& SurfaceHit, float SplashRadius)
		{
			AffectedCount += ApplyStatusInArea(
				SurfaceHit.ImpactPoint,
				SurfaceHit.ImpactNormal,
				SplashRadius,
				Status,
				Duration,
				Origin,
				ProcessedCoords,
				Instigator,
				Tier);
		});

	UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyElementalBurst FINISH -> AffectedCount: %d"), AffectedCount);
	return AffectedCount;
}

void UDungeonSurfaceSubsystem::ProcessGridTick()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	// 1. Wygaszanie przeterminowanych statusów w komórkach
	ExpireCellStatuses(CurrentTime);

	// 2. Propagacja żywiołów na sąsiednie komórki (Cellular Automata - nośniki płynne)
	PropagateElementalSpreads(CurrentTime, SafeCellSize);

	// 3. Rozprzestrzenianie ognia stałego paliwa (np. drewno) w interwale czasowym
	ProcessSolidFuelCombustion(CurrentTime, SafeCellSize);

	// 4. Server-Authoritative: dwukierunkowa interakcja z postaciami oraz aplikacja obrażeń do fundamentów
	if (World->GetNetMode() != NM_Client && ActiveCells.Num() > 0)
	{
		ProcessActorInteractions(CurrentTime);
		ProcessSurfaceStructuralDamage(CurrentTime, SubsystemTickInterval);
	}

	// 5. Debug visuals
	DrawDebugVisuals();
}

void UDungeonSurfaceSubsystem::ExpireCellStatuses(float CurrentTime)
{
	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		FSurfaceCellData& Cell = It.Value();
		bool bStatusRemoved = false;
		for (int32 Index = Cell.ActiveStatuses.Num() - 1; Index >= 0; --Index)
		{
			if (Cell.ActiveStatuses[Index].IsExpired(CurrentTime))
			{
				UE_LOG(LogDungeonElements, Verbose, TEXT("[ProcessGridTick] Cell Coord(%d,%d,%d Face:%d) status %s expired (EndTime: %.1fs, Current: %.1fs)"),
					It.Key().X, It.Key().Y, It.Key().Z, static_cast<int32>(It.Key().Face),
					*UEnum::GetValueAsString(Cell.ActiveStatuses[Index].Status),
					Cell.ActiveStatuses[Index].ServerEndTime, CurrentTime);
				Cell.ActiveStatuses.RemoveAt(Index);
				bStatusRemoved = true;
			}
		}

		// Jeśli wygasł status nośnika (np. woda wyschła),
		// sprawdzamy czy pozostałe w komórce statusy zależne mogą nadal legalnie istnieć na tym materiale
		if (bStatusRemoved && !Cell.IsEmpty())
		{
			USurfaceCellTransitionUtils::CleanOrphanedStatuses(Cell);
		}

		if (Cell.IsEmpty())
		{
			UE_LOG(LogDungeonElements, Verbose, TEXT("[ProcessGridTick] Cell Coord(%d,%d,%d Face:%d) became EMPTY and was removed from grid"),
				It.Key().X, It.Key().Y, It.Key().Z, static_cast<int32>(It.Key().Face));
			OnSurfaceCellChanged.Broadcast(It.Key(), EStatusEffectType::None, nullptr);
			It.RemoveCurrent();
		}
		else if (bStatusRemoved)
		{
			OnSurfaceCellChanged.Broadcast(It.Key(), Cell.GetDominantStatus(), Cell.GetDominantInstigator());
		}
	}
}

void UDungeonSurfaceSubsystem::PropagateElementalSpreads(float CurrentTime, float SafeCellSize)
{
	TMap<FSurfaceCellCoord, FPendingSpreadCell> PendingSpreads;
	TArray<FSurfaceCellCoord> NeighborCoords;

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& SourceCoord = Pair.Key;
		const FSurfaceCellData& SourceData = Pair.Value;

		if (SourceData.IsEmpty())
		{
			continue;
		}

		SourceCoord.GetAdjacentNeighbors(NeighborCoords);

		for (const FSurfaceCellCoord& NeighborCoord : NeighborCoords)
		{
			// Bezpiecznik 1: pomijamy komórki w tym samym voxelu (X, Y, Z)
			if (NeighborCoord.X == SourceCoord.X && NeighborCoord.Y == SourceCoord.Y && NeighborCoord.Z == SourceCoord.Z)
			{
				continue;
			}

			const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord);
			if (!NeighborData || NeighborData->IsEmpty())
			{
				continue;
			}

			// Bezpiecznik 2: ta sama struktura nie może rozprzestrzeniać statusów między swoimi wewnętrznymi prostopadłymi płaszczyznami
			if (NeighborCoord.Face != SourceCoord.Face && NeighborData->SurfaceActor.IsValid() && NeighborData->SurfaceActor == SourceData.SurfaceActor)
			{
				continue;
			}

			// Bezpiecznik 3 fizyczny w 3D: odległość między centrami powierzchni musi być <= 1.5 * SafeCellSize (np. 75 cm)
			const FVector SourceSurfacePos = SourceCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(SourceCoord.Face) * (SafeCellSize * 0.45f);
			const FVector NeighborSurfacePos = NeighborCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(NeighborCoord.Face) * (SafeCellSize * 0.45f);
			if (FVector::DistSquared(SourceSurfacePos, NeighborSurfacePos) > FMath::Square(SafeCellSize * 1.5f))
			{
				continue;
			}

			for (const FSurfaceCellStatusEntry& SourceEntry : SourceData.ActiveStatuses)
			{
				FPendingSpreadCell Spread;
				if (TryEvaluateSpreadReaction(NeighborCoord, *NeighborData, SourceEntry, CurrentTime, Spread))
				{
					if (FPendingSpreadCell* ExistingPending = PendingSpreads.Find(NeighborCoord))
					{
						// Jeśli mamy już oczekujący spread na tę komórkę, preferujemy ten o tym samym statusie
						if (Spread.NewStatus == ExistingPending->NewStatus)
						{
							if (Spread.Duration == 0.0f || ExistingPending->Duration == 0.0f)
							{
								ExistingPending->Duration = 0.0f;
							}
							else
							{
								ExistingPending->Duration = FMath::Max(ExistingPending->Duration, Spread.Duration);
							}
						}
					}
					else
					{
						PendingSpreads.Add(NeighborCoord, Spread);
					}
				}
			}
		}
	}

	// Aplikacja zebranych rozprzestrzenień żywiołów przez jednolity punkt styku
	for (const auto& PendingPair : PendingSpreads)
	{
		const FPendingSpreadCell& Pending = PendingPair.Value;
		ApplyStatusToCell(Pending.Coord, Pending.NewStatus, Pending.Duration, Pending.Instigator.Get());
	}
}

void UDungeonSurfaceSubsystem::ProcessSolidFuelCombustion(float CurrentTime, float SafeCellSize)
{
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

		// Inicjalizacja czasu pierwszego rozprzestrzenienia jeśli był zerowy
		if (SourceData.NextFuelSpreadTime <= 0.0f)
		{
			SourceData.NextFuelSpreadTime = CurrentTime + Traits.FuelSpreadInterval;
			continue;
		}

		// Sprawdzamy interwał rozprzestrzeniania (np. co 2.0s)
		if (CurrentTime < SourceData.NextFuelSpreadTime)
		{
			continue;
		}

		// Resetujemy licznik do następnej próby za kolejne FuelSpreadInterval sekund
		SourceData.NextFuelSpreadTime = CurrentTime + Traits.FuelSpreadInterval;

		// Pytamy wyspecjalizowaną geometrię o legalnych fizycznie i topologicznie kandydatów w siatce
		TArray<SurfaceGridGeometryUtils::FSurfaceSpreadCandidate> Candidates;
		SurfaceGridGeometryUtils::FindSpreadCandidates(GetWorld(), SourceCoord, SourceData.SurfaceActor.Get(), ActiveCells, SafeCellSize, Candidates);

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

						UE_LOG(LogDungeonElements, Log, TEXT("[SolidFuel] Spread %s -> (%d, %d, %d, %s) on Actor: %s (Tier: %d)"),
							Candidate.bIsCorner ? TEXT("Corner 90°") : TEXT("Coplanar"),
							Candidate.Coord.X, Candidate.Coord.Y, Candidate.Coord.Z,
							*UEnum::GetValueAsString(Candidate.Coord.Face),
							Candidate.SurfaceActor ? *Candidate.SurfaceActor->GetName() : TEXT("None"),
							BurningEntry->Tier);
					}
				}
			}
		}
	}

	// Aplikujemy zapłony stałego paliwa przez atomowy punkt styku
	const float BaseDuration = UElementalReactionRules::GetEffectConfig(EStatusEffectType::Burning).GetBaseDuration();
	for (const auto& Pair : PendingFuelSpreads)
	{
		const FPendingFuelSpread& Spread = Pair.Value;
		ApplyStatusToCell(Spread.Coord, EStatusEffectType::Burning, BaseDuration, Spread.Instigator.Get(), Spread.Material, Spread.SurfaceActor.Get(), Spread.Tier);
	}
}

void UDungeonSurfaceSubsystem::ProcessSurfaceStructuralDamage(float CurrentTime, float DeltaTime)
{
	if (ActiveCells.IsEmpty() || DeltaTime <= 0.0f)
	{
		return;
	}

	// 1. Zbieramy i agregujemy obrażenia per unikalny aktor architektury lochu oraz typ obrażeń.
	// Klucz: TPair<TWeakObjectPtr<AActor>, EDamageType>, Wartość: Suma obrażeń na sekundę (DPS) oraz ostatni instigator
	struct FDamageAggregate
	{
		float TotalDPS = 0.0f;
		TWeakObjectPtr<AActor> LastInstigator = nullptr;
	};

	TMap<TPair<TWeakObjectPtr<AActor>, EDamageType>, FDamageAggregate> AggregatedDamage;

	constexpr float MaxStructuralDPS = 25.0f; // Górny limit DPS na pojedynczą strukturę lochu dla danego typu obrażeń

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellData& Cell = Pair.Value;
		if (Cell.IsEmpty() || !Cell.SurfaceActor.IsValid())
		{
			continue;
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
	}

	// 2. Aplikujemy zagregowane pakiety obrażeń do UDamageableComponent fundamentów
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

		// UDamageableComponent automatycznie uwzględnia TotalResistance (np. 100% dla kamienia, 0% dla drewna na ogień, 50% na prąd)
		DmgComp->ApplyDamage(DamageAmount, DmgType, Agg.LastInstigator.Get());
	}
}

void UDungeonSurfaceSubsystem::ProcessActorInteractions(float CurrentTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (RegisteredStatusComponents.Num() == 0)
	{
		return;
	}

	// Kopia lokalna (snapshot) wskaźników słabych.
	// Kluczowa ochrona: jeśli wybuch/reakcja w trakcie pętli zniszczy aktora (np. wybuchająca beczka),
	// jego EndPlay wywoła UnregisterStatusComponent modyfikujący RegisteredStatusComponents.
	// Dzięki kopii lokalnej nie ryzykujemy błędu Array Index Out of Bounds.
	const TArray<TWeakObjectPtr<UStatusEffectComponent>> ComponentsToProcess = RegisteredStatusComponents;

	for (const TWeakObjectPtr<UStatusEffectComponent>& WeakComp : ComponentsToProcess)
	{
		UStatusEffectComponent* StatusComp = WeakComp.Get();
		if (!StatusComp || !IsValid(StatusComp))
		{
			continue;
		}

		AActor* OwnerActor = StatusComp->GetOwner();
		if (!OwnerActor || OwnerActor->IsActorBeingDestroyed())
		{
			continue;
		}

		ProcessActorInteraction(OwnerActor, StatusComp, CurrentTime);
	}

	// Czyszczenie martwych wskaźników z głównego rejestru podsystemu
	for (int32 Idx = RegisteredStatusComponents.Num() - 1; Idx >= 0; --Idx)
	{
		if (!RegisteredStatusComponents[Idx].IsValid())
		{
			RegisteredStatusComponents.RemoveAt(Idx);
		}
	}
}

void UDungeonSurfaceSubsystem::ProcessActorInteraction(AActor* Actor, UStatusEffectComponent* StatusComp, float CurrentTime)
{
	if (!Actor || Actor->IsActorBeingDestroyed() || !StatusComp)
	{
		return;
	}

	TArray<FSurfaceCellCoord> TouchedCells;
	SurfaceGridProjectionUtils::GetCellsTouchingActor(Actor, ActiveCells, CellSize, TouchedCells);
	if (TouchedCells.Num() == 0)
	{
		return;
	}

	// 1. Interakcja Obiekt -> Podłoże (np. płonący gracz zapala olej pod stopami)
	TArray<EStatusEffectType> ActorStatuses = StatusComp->GetActiveStatuses();
	UElementalReactionRules::SortByReactionPriority(ActorStatuses);
	SurfaceActorInteractionUtils::ApplyActorEffectsToFloor(
		Actor,
		StatusComp,
		TouchedCells,
		ActiveCells,
		ActorStatuses,
		[this](const FSurfaceCellCoord& Coord, EStatusEffectType Status, float Duration, AActor* Instigator)
		{
			return ApplyStatusToCell(Coord, Status, Duration, Instigator);
		});

	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
	{
		return;
	}

	// 2. Interakcja Podłoże -> Obiekt (kałuża wody gasi gracza lub kałuża ognia podpala)
	SurfaceActorInteractionUtils::ApplyFloorEffectsToActor(Actor, StatusComp, TouchedCells, ActiveCells, CellSize);
}

void UDungeonSurfaceSubsystem::DrawDebugVisuals() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDrawDebugGrid || !GetWorld())
	{
		return;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float DebugLifeTime = SubsystemTickInterval + 0.05f;

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FSurfaceCellData& Data = Pair.Value;

		if (Data.IsEmpty())
		{
			continue;
		}

		FColor Color;
		switch (Data.GetDominantStatus())
		{
		case EStatusEffectType::Burning:     Color = FColor(255, 69, 0);   break;
		case EStatusEffectType::Wet:         Color = FColor(30, 144, 255); break;
		case EStatusEffectType::Oiled:       Color = FColor(139, 69, 19);  break;
		case EStatusEffectType::Electrified: Color = FColor(255, 215, 0);  break;
		default:                             Color = FColor(200, 200, 200); break;
		}

		// Wizualizacja koegzystencji prądu i wody (Cyan / Electric Blue)
		if (Data.HasStatus(EStatusEffectType::Wet) && Data.HasStatus(EStatusEffectType::Electrified))
		{
			Color = FColor(0, 255, 255);
		}

		const FVector Center = Coord.ToWorldLocation(SafeCellSize);
		const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);

		// Lekki offset od powierzchni, by uniknąć z-fightingu
		const FVector VisualCenter = Center + Normal * 3.0f;
		const FVector HalfExtent = FVector(SafeCellSize * 0.42f);

		DrawDebugBox(GetWorld(), VisualCenter, HalfExtent, Color, false, DebugLifeTime, 0, 2.0f);
	}
#endif
}

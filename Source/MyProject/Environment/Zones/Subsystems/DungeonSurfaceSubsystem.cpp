#include "DungeonSurfaceSubsystem.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Logging/DungeonLogCategories.h"

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

int32 UDungeonSurfaceSubsystem::PaintSurface(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator)
{
	if (!GetWorld() || Status == EStatusEffectType::None || Radius <= 0.0f || Duration <= 0.0f)
	{
		return 0;
	}

	const ESurfaceFaceDirection FaceDir = SurfaceGridUtils::NormalToFaceDirection(HitNormal);
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(FaceDir);

	// Wyznaczamy wektory styczne do płaszczyzny ściany/podłogi
	FVector TangentU;
	FVector TangentV;

	if (FaceDir == ESurfaceFaceDirection::Up || FaceDir == ESurfaceFaceDirection::Down)
	{
		TangentU = FVector(1.0f, 0.0f, 0.0f);
		TangentV = FVector(0.0f, 1.0f, 0.0f);
	}
	else if (FaceDir == ESurfaceFaceDirection::North || FaceDir == ESurfaceFaceDirection::South)
	{
		TangentU = FVector(0.0f, 1.0f, 0.0f);
		TangentV = FVector(0.0f, 0.0f, 1.0f);
	}
	else
	{
		TangentU = FVector(1.0f, 0.0f, 0.0f);
		TangentV = FVector(0.0f, 0.0f, 1.0f);
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const int32 StepRadius = FMath::CeilToInt(Radius / SafeCellSize);
	const float RadiusSq = FMath::Square(Radius);
	const float CurrentTime = GetWorld()->GetTimeSeconds();

	int32 AffectedCount = 0;

	for (int32 du = -StepRadius; du <= StepRadius; ++du)
	{
		for (int32 dv = -StepRadius; dv <= StepRadius; ++dv)
		{
			const FVector Offset = (static_cast<float>(du) * TangentU + static_cast<float>(dv) * TangentV) * SafeCellSize;
			if (Offset.SizeSquared() > RadiusSq)
			{
				continue;
			}

			const FVector SamplePoint = HitLocation + Offset;
			const FSurfaceCellCoord Coord = FSurfaceCellCoord::FromWorldLocation(SamplePoint, Normal, SafeCellSize);

			if (FSurfaceCellData* Existing = ActiveCells.Find(Coord))
			{
				if (Existing->Status == Status)
				{
					// Identyczny żywioł: odświeżamy czas trwania
					Existing->ServerEndTime = FMath::Max(Existing->ServerEndTime, CurrentTime + Duration);
					Existing->Instigator = Instigator;
					AffectedCount++;
				}
				else if (Existing->Status != EStatusEffectType::None)
				{
					// Różny żywioł: ewaluacja chemiczna przez centralną bibliotekę UElementalChemistryLibrary
					const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(Status, { Existing->Status });
					if (Reaction.bReactionOccurred)
					{
						if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
						{
							// Dotychczasowy status uległ spaleniu/wyparciu (np. Olej zamieniony w Ogień)
							const EStatusEffectType NewStatus = (Reaction.ResultingStatus != EStatusEffectType::None) ? Reaction.ResultingStatus : Status;
							const float NewDuration = (Reaction.ResultingDuration > 0.0f) ? Reaction.ResultingDuration : Duration;
							Existing->Status = NewStatus;
							Existing->ServerEndTime = CurrentTime + NewDuration;
							Existing->Instigator = Instigator;
							OnSurfaceCellChanged.Broadcast(Coord, NewStatus, Instigator);
							AffectedCount++;
						}
						else if (Reaction.bConsumeIncomingStatus)
						{
							// Przychodzący status został zneutralizowany (np. ogień odparowany przez wodę)
							// Komórka zachowuje dotychczasowy stan
						}
						else
						{
							const EStatusEffectType NewStatus = (Reaction.ResultingStatus != EStatusEffectType::None) ? Reaction.ResultingStatus : Status;
							const float NewDuration = (Reaction.ResultingDuration > 0.0f) ? Reaction.ResultingDuration : Duration;
							Existing->Status = NewStatus;
							Existing->ServerEndTime = CurrentTime + NewDuration;
							Existing->Instigator = Instigator;
							OnSurfaceCellChanged.Broadcast(Coord, NewStatus, Instigator);
							AffectedCount++;
						}
					}
					else
					{
						// Brak reguły specjalnej: nowy status zastępuje poprzedni
						Existing->Status = Status;
						Existing->ServerEndTime = CurrentTime + Duration;
						Existing->Instigator = Instigator;
						OnSurfaceCellChanged.Broadcast(Coord, Status, Instigator);
						AffectedCount++;
					}
				}
			}
			else
			{
				// Nowa komórka w siatce
				FSurfaceCellData NewCell;
				NewCell.Status = Status;
				NewCell.ServerEndTime = CurrentTime + Duration;
				NewCell.Instigator = Instigator;

				ActiveCells.Add(Coord, NewCell);
				OnSurfaceCellChanged.Broadcast(Coord, Status, Instigator);
				AffectedCount++;
			}
		}
	}

	return AffectedCount;
}

int32 UDungeonSurfaceSubsystem::ClearCellsInBounds(const FBox& BoundingBox)
{
	int32 RemovedCount = 0;
	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		const FVector Center = It.Key().ToWorldLocation(SafeCellSize);
		if (BoundingBox.IsInsideOrOn(Center))
		{
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

bool UDungeonSurfaceSubsystem::QueryStatusAt(const FVector& WorldLocation, EStatusEffectType& OutStatus, AActor*& OutInstigator) const
{
	OutStatus = EStatusEffectType::None;
	OutInstigator = nullptr;

	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	// 1. Sprawdzamy podłoże pod stopami (+Z Floor)
	// Próbkujemy zarówno zadaną wysokość Z, jak i próbki z lekkim offsetem pionowym (na tolerancję kolizji i progów)
	const float SampleZOffsets[3] = { 0.0f, -15.0f, 15.0f };
	for (float ZOffset : SampleZOffsets)
	{
		const FVector SamplePos = WorldLocation + FVector(0.0f, 0.0f, ZOffset);
		const FSurfaceCellCoord FloorCoord = FSurfaceCellCoord::FromWorldLocation(SamplePos, FVector::UpVector, SafeCellSize);
		if (const FSurfaceCellData* Found = ActiveCells.Find(FloorCoord))
		{
			if (Found->Status != EStatusEffectType::None)
			{
				OutStatus = Found->Status;
				OutInstigator = Found->Instigator.Get();
				return true;
			}
		}
	}

	// 2. Sprawdzamy 4 sąsiednie kierunki na wysokości korpusu (ściany, filary w promieniu dotyku)
	const ESurfaceFaceDirection WallFaces[4] = {
		ESurfaceFaceDirection::North,
		ESurfaceFaceDirection::South,
		ESurfaceFaceDirection::East,
		ESurfaceFaceDirection::West
	};

	for (ESurfaceFaceDirection Face : WallFaces)
	{
		const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Face);
		const FSurfaceCellCoord WallCoord = FSurfaceCellCoord::FromWorldLocation(WorldLocation + Normal * (SafeCellSize * 0.4f), Normal, SafeCellSize);
		if (const FSurfaceCellData* Found = ActiveCells.Find(WallCoord))
		{
			if (Found->Status != EStatusEffectType::None)
			{
				OutStatus = Found->Status;
				OutInstigator = Found->Instigator.Get();
				return true;
			}
		}
	}

	return false;
}

void UDungeonSurfaceSubsystem::GetCellsTouchingActor(const AActor* Actor, TArray<FSurfaceCellCoord>& OutCoords) const
{
	OutCoords.Reset();

	if (!Actor)
	{
		return;
	}

	FBoxSphereBounds Bounds;
	if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		Bounds = RootPrim->Bounds;
	}
	else
	{
		Bounds = Actor->GetComponentsBoundingBox(true);
	}

	if (Bounds.BoxExtent.IsNearlyZero())
	{
		Bounds.Origin = Actor->GetActorLocation();
		Bounds.BoxExtent = FVector(34.0f, 34.0f, 88.0f);
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float FootZ = Bounds.Origin.Z - Bounds.BoxExtent.Z + 10.0f;
	const float HalfExtentX = Bounds.BoxExtent.X * 0.6f;
	const float HalfExtentY = Bounds.BoxExtent.Y * 0.6f;

	const FVector ProbePoints[6] = {
		FVector(Bounds.Origin.X, Bounds.Origin.Y, FootZ),
		FVector(Bounds.Origin.X + HalfExtentX, Bounds.Origin.Y, FootZ),
		FVector(Bounds.Origin.X - HalfExtentX, Bounds.Origin.Y, FootZ),
		FVector(Bounds.Origin.X, Bounds.Origin.Y + HalfExtentY, FootZ),
		FVector(Bounds.Origin.X, Bounds.Origin.Y - HalfExtentY, FootZ),
		Bounds.Origin
	};

	TSet<FSurfaceCellCoord> UniqueCoords;

	for (const FVector& Pt : ProbePoints)
	{
		// Podłoga (+Z Floor)
		const float SampleZOffsets[3] = { 0.0f, -15.0f, 15.0f };
		for (float ZOffset : SampleZOffsets)
		{
			const FVector SamplePos = Pt + FVector(0.0f, 0.0f, ZOffset);
			const FSurfaceCellCoord FloorCoord = FSurfaceCellCoord::FromWorldLocation(SamplePos, FVector::UpVector, SafeCellSize);
			if (ActiveCells.Contains(FloorCoord))
			{
				UniqueCoords.Add(FloorCoord);
			}
		}

		// Ściany wokół próbki
		const ESurfaceFaceDirection WallFaces[4] = {
			ESurfaceFaceDirection::North,
			ESurfaceFaceDirection::South,
			ESurfaceFaceDirection::East,
			ESurfaceFaceDirection::West
		};

		for (ESurfaceFaceDirection Face : WallFaces)
		{
			const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Face);
			const FSurfaceCellCoord WallCoord = FSurfaceCellCoord::FromWorldLocation(Pt + Normal * (SafeCellSize * 0.4f), Normal, SafeCellSize);
			if (ActiveCells.Contains(WallCoord))
			{
				UniqueCoords.Add(WallCoord);
			}
		}
	}

	OutCoords = UniqueCoords.Array();
}

bool UDungeonSurfaceSubsystem::QueryStatusForActor(const AActor* Actor, EStatusEffectType& OutStatus, AActor*& OutInstigator) const
{
	OutStatus = EStatusEffectType::None;
	OutInstigator = nullptr;

	TArray<FSurfaceCellCoord> TouchedCells;
	GetCellsTouchingActor(Actor, TouchedCells);

	for (const FSurfaceCellCoord& Coord : TouchedCells)
	{
		if (const FSurfaceCellData* Found = ActiveCells.Find(Coord))
		{
			if (Found->Status != EStatusEffectType::None)
			{
				OutStatus = Found->Status;
				OutInstigator = Found->Instigator.Get();
				return true;
			}
		}
	}

	return false;
}

int32 UDungeonSurfaceSubsystem::ApplyElementalBurst(
	const FVector& Origin,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator)
{
	if (!GetWorld() || Status == EStatusEffectType::None || Radius <= 0.0f)
	{
		return 0;
	}

	const float RadiusSq = FMath::Square(Radius);
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	int32 AffectedCount = 0;

	// 1. Reakcja chemiczna ze wszystkimi aktywnymi komórkami w promieniu wybuchu
	TArray<FSurfaceCellCoord> CoordsToRemove;
	for (auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		FSurfaceCellData& Data = Pair.Value;

		const FVector CellWorldPos = Coord.ToWorldLocation(SafeCellSize);
		if (FVector::DistSquared(Origin, CellWorldPos) <= RadiusSq)
		{
			if (Data.Status == Status)
			{
				Data.ServerEndTime = FMath::Max(Data.ServerEndTime, CurrentTime + Duration);
				Data.Instigator = Instigator;
				AffectedCount++;
			}
			else if (Data.Status != EStatusEffectType::None)
			{
				const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(Status, { Data.Status });
				if (Reaction.bReactionOccurred)
				{
					if (Reaction.ResultingStatus != EStatusEffectType::None)
					{
						Data.Status = Reaction.ResultingStatus;
						Data.ServerEndTime = CurrentTime + (Reaction.ResultingDuration > 0.0f ? Reaction.ResultingDuration : Duration);
						Data.Instigator = Instigator;
						OnSurfaceCellChanged.Broadcast(Coord, Reaction.ResultingStatus, Instigator);
						AffectedCount++;
					}
					else if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
					{
						CoordsToRemove.Add(Coord);
					}
				}
			}
		}
	}

	for (const FSurfaceCellCoord& Coord : CoordsToRemove)
	{
		OnSurfaceCellChanged.Broadcast(Coord, EStatusEffectType::None, Instigator);
		ActiveCells.Remove(Coord);
		AffectedCount++;
	}

	// 2. Jeśli wybuch nastąpił w pobliżu powierzchni (np. posadzka lochu), malujemy strefę żywiołu
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfaceBurstTrace), false, Instigator);
	FHitResult FloorHit;
	if (GetWorld()->LineTraceSingleByChannel(FloorHit, Origin, Origin - FVector(0.0f, 0.0f, Radius + 100.0f), ECC_Visibility, TraceParams))
	{
		AffectedCount += PaintSurface(FloorHit.ImpactPoint, FloorHit.ImpactNormal, Radius * 0.75f, Status, Duration, Instigator);
	}

	return AffectedCount;
}

void UDungeonSurfaceSubsystem::ClearAllCells()
{
	ActiveCells.Empty();
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

	// 1. Wygaszanie przeterminowanych komórek
	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		if (It.Value().ServerEndTime > 0.0f && CurrentTime >= It.Value().ServerEndTime)
		{
			OnSurfaceCellChanged.Broadcast(It.Key(), EStatusEffectType::None, nullptr);
			It.RemoveCurrent();
		}
	}

	// 2. Propagacja żywiołów na sąsiednie komórki (Cellular Automata)
	// Wyłącznie reguły z UElementalChemistryLibrary - zero twardego kodowania statusów w podsystemie
	struct FPendingSpreadCell
	{
		FSurfaceCellCoord Coord;
		EStatusEffectType NewStatus = EStatusEffectType::None;
		float Duration = 0.0f;
		TWeakObjectPtr<AActor> Instigator = nullptr;
	};

	TMap<FSurfaceCellCoord, FPendingSpreadCell> PendingSpreads;
	TArray<FSurfaceCellCoord> NeighborCoords;

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& SourceCoord = Pair.Key;
		const FSurfaceCellData& SourceData = Pair.Value;

		if (SourceData.Status == EStatusEffectType::None)
		{
			continue;
		}

		SourceCoord.GetAdjacentNeighbors(NeighborCoords);

		for (const FSurfaceCellCoord& NeighborCoord : NeighborCoords)
		{
			if (const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord))
			{
				if (NeighborData->Status == EStatusEffectType::None || NeighborData->Status == SourceData.Status)
				{
					continue;
				}

				// Bezpiecznik fizyczny w 3D: odległość między centrami powierzchni musi być <= 1.5 * SafeCellSize (np. 75 cm)
				const FVector SourceSurfacePos = SourceCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(SourceCoord.Face) * (SafeCellSize * 0.45f);
				const FVector NeighborSurfacePos = NeighborCoord.ToWorldLocation(SafeCellSize) + SurfaceGridUtils::FaceDirectionToNormal(NeighborCoord.Face) * (SafeCellSize * 0.45f);
				if (FVector::DistSquared(SourceSurfacePos, NeighborSurfacePos) > FMath::Square(SafeCellSize * 1.5f))
				{
					continue;
				}

				// Pytamy bibliotekę chemii, czy żywioł może rozprzestrzenić się na sąsiednią komórkę
				FElementalReactionResult SpreadReaction;
				if (UElementalChemistryLibrary::CanSpreadToNeighbor(SourceData.Status, NeighborData->Status, SpreadReaction))
				{
					FPendingSpreadCell& Pending = PendingSpreads.FindOrAdd(NeighborCoord);
					Pending.Coord = NeighborCoord;
					Pending.NewStatus = (SpreadReaction.ResultingStatus != EStatusEffectType::None) ? SpreadReaction.ResultingStatus : SourceData.Status;
					Pending.Duration = (SpreadReaction.ResultingDuration > 0.0f) ? SpreadReaction.ResultingDuration : 5.0f;
					Pending.Instigator = SourceData.Instigator;
				}
			}
		}
	}

	// Aplikacja zebranych rozprzestrzenień żywiołów
	for (const auto& PendingPair : PendingSpreads)
	{
		const FPendingSpreadCell& Pending = PendingPair.Value;
		if (FSurfaceCellData* CellToUpdate = ActiveCells.Find(Pending.Coord))
		{
			CellToUpdate->Status = Pending.NewStatus;
			CellToUpdate->ServerEndTime = CurrentTime + Pending.Duration;
			CellToUpdate->Instigator = Pending.Instigator;
			OnSurfaceCellChanged.Broadcast(Pending.Coord, Pending.NewStatus, Pending.Instigator.Get());
		}
	}

	// 3. Server-Authoritative: dwukierunkowa interakcja żywiołowa między postaciami a komórkami
	if (World->GetNetMode() != NM_Client)
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* Pawn = *It;
			if (!Pawn || Pawn->IsActorBeingDestroyed())
			{
				continue;
			}

			TArray<FSurfaceCellCoord> TouchedCells;
			GetCellsTouchingActor(Pawn, TouchedCells);
			if (TouchedCells.Num() == 0)
			{
				continue;
			}

			UStatusEffectComponent* StatusComp = Pawn->FindComponentByClass<UStatusEffectComponent>();
			const TArray<EStatusEffectType> PawnStatuses = StatusComp ? StatusComp->GetActiveStatuses() : TArray<EStatusEffectType>();

			for (const FSurfaceCellCoord& CellCoord : TouchedCells)
			{
				FSurfaceCellData* CellData = ActiveCells.Find(CellCoord);
				if (!CellData || CellData->Status == EStatusEffectType::None)
				{
					continue;
				}

				// A. Interakcja Postać -> Komórka (np. podpalona postać podpala kałużę oleju pod stopami)
				for (EStatusEffectType PawnStatus : PawnStatuses)
				{
					if (PawnStatus == CellData->Status || PawnStatus == EStatusEffectType::None)
					{
						continue;
					}

					const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(PawnStatus, { CellData->Status });
					if (Reaction.bReactionOccurred)
					{
						if (Reaction.ResultingStatus != EStatusEffectType::None)
						{
							CellData->Status = Reaction.ResultingStatus;
							CellData->ServerEndTime = CurrentTime + (Reaction.ResultingDuration > 0.0f ? Reaction.ResultingDuration : 5.0f);
							CellData->Instigator = Pawn;
							OnSurfaceCellChanged.Broadcast(CellCoord, Reaction.ResultingStatus, Pawn);
						}
						else if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
						{
							OnSurfaceCellChanged.Broadcast(CellCoord, EStatusEffectType::None, Pawn);
							ActiveCells.Remove(CellCoord);
							break;
						}
					}
				}

				// B. Interakcja Komórka -> Postać (nakładanie/odświeżanie statusu z posadzki na postać)
				if (FSurfaceCellData* CurrentCell = ActiveCells.Find(CellCoord))
				{
					if (CurrentCell->Status != EStatusEffectType::None && StatusComp)
					{
						StatusComp->ApplyStatus(CurrentCell->Status, 1.5f, CurrentCell->Instigator.Get());
					}
				}
			}
		}
	}

	// 4. Debug visuals
	DrawDebugVisuals();
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

		FColor Color;
		switch (Data.Status)
		{
		case EStatusEffectType::Burning:     Color = FColor(255, 69, 0);   break;
		case EStatusEffectType::Wet:         Color = FColor(30, 144, 255); break;
		case EStatusEffectType::Oiled:       Color = FColor(139, 69, 19);  break;
		case EStatusEffectType::Electrified: Color = FColor(255, 215, 0);  break;
		default:                             Color = FColor(200, 200, 200); break;
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

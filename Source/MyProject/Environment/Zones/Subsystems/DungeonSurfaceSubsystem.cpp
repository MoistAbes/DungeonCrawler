#include "DungeonSurfaceSubsystem.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "Engine/Brush.h"
#include "MyProject/Logging/DungeonLogCategories.h"

namespace
{
	/** Sprawdza, czy w danym punkcie fizycznie istnieje płaszczyzna architektury (Drop-off test na krawędziach filarów i ścian) */
	static bool CheckSurfacePresenceAt(
		const UWorld* World,
		const FVector& SamplePoint,
		const FVector& SurfaceNormal,
		FHitResult& OutHit,
		const FCollisionQueryParams& Params)
	{
		if (!World)
		{
			return false;
		}

		const FVector ProbeStart = SamplePoint + SurfaceNormal * 20.0f;
		const FVector ProbeEnd = SamplePoint - SurfaceNormal * 30.0f;

		if (World->LineTraceSingleByChannel(OutHit, ProbeStart, ProbeEnd, ECC_WorldStatic, Params))
		{
			if (OutHit.GetActor() && UDungeonSurfaceSubsystem::IsValidSurfaceTarget(OutHit.GetActor()))
			{
				const float NormalDot = FVector::DotProduct(OutHit.ImpactNormal, SurfaceNormal);
				if (NormalDot > 0.65f)
				{
					const float DistFromPlane = FMath::Abs(FVector::DotProduct(OutHit.ImpactPoint - SamplePoint, SurfaceNormal));
					return (DistFromPlane < 25.0f);
				}
			}
		}

		return false;
	}

	/** Sprawdza, czy między punktem uderzenia a próbką na powierzchni nie ma przeszkody pionowej (LoS test - filary, narożniki) */
	static bool HasSurfaceLineOfSight(
		const UWorld* World,
		const FVector& StartLocation,
		const FVector& TargetLocation,
		const FVector& SurfaceNormal,
		const FCollisionQueryParams& Params)
	{
		if (!World)
		{
			return false;
		}

		const FVector LoSStart = StartLocation + SurfaceNormal * 10.0f;
		const FVector LoSEnd = TargetLocation + SurfaceNormal * 10.0f;

		FHitResult LoSHit;
		if (World->LineTraceSingleByChannel(LoSHit, LoSStart, LoSEnd, ECC_WorldStatic, Params))
		{
			// Wykrywamy przeszkody poprzeczne/pionowe do powierzchni (ściany, kolumny)
			const bool bIsObstacle = FMath::Abs(FVector::DotProduct(LoSHit.ImpactNormal, SurfaceNormal)) < 0.6f;
			if (bIsObstacle && LoSHit.Distance < FVector::Dist(LoSStart, LoSEnd) - 10.0f)
			{
				return false;
			}
		}

		return true;
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
	if (!Actor)
	{
		return false;
	}

	// 1. Wykluczamy postacie oraz dynamiczne/interaktywne rekwizyty lochu (beczki, skrzynie)
	if (Actor->IsA<APawn>() || Actor->IsA<AInteractivePropBase>())
	{
		return false;
	}

	// 2. Akceptujemy oficjalne fundamenty i architekturę lochu (ściany, podłogi, sufity)
	if (Actor->IsA<ADungeonStructureBase>())
	{
		return true;
	}

	// 3. Akceptujemy geometrię poziomu (BSP Brushes map testowych i prototypowych)
	if (Actor->IsA<ABrush>())
	{
		return true;
	}

	return false;
}

int32 UDungeonSurfaceSubsystem::PaintSurfaceFromHit(
	const FHitResult& HitResult,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !HitResult.bBlockingHit)
	{
		return 0;
	}

	AActor* HitActor = HitResult.GetActor();

	// 1. Jeśli uderzono bezpośrednio w postać lub rekwizyt
	if (HitActor)
	{
		// Trafienie w istniejącą strefę przestrzenną (np. gaz, dym)
		if (AStatusZoneBase* ExistingZone = Cast<AStatusZoneBase>(HitActor))
		{
			ExistingZone->ApplyElementalHit(Status, 0.0f, Instigator);
			return 0;
		}

		if (Status != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = HitActor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(Status, Duration, Instigator);
			}
		}

		// Jeśli trafiliśmy w postać lub rekwizyt, szukamy posadzki pod nim, aby rozlać ciecz pod stopami
		if (HitActor->IsA<APawn>() || HitActor->IsA<AInteractivePropBase>())
		{
			const FVector ActorLocation = HitActor->GetActorLocation();
			FHitResult FloorHit;
			FCollisionQueryParams FloorTraceParams(SCENE_QUERY_STAT(SurfaceGridFloorTrace), false, HitActor);
			FloorTraceParams.AddIgnoredActor(HitActor);
			if (Instigator)
			{
				FloorTraceParams.AddIgnoredActor(Instigator);
			}

			const FVector TraceStart = ActorLocation;
			const FVector TraceEnd = ActorLocation - FVector(0.0f, 0.0f, 300.0f);

			if (World->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd, ECC_WorldStatic, FloorTraceParams))
			{
				return PaintSurfaceFromHit(FloorHit, Radius, Status, Duration, Instigator);
			}
			else
			{
				return 0;
			}
		}
	}

	// Strefa powierzchniowa może powstać WYŁĄCZNIE na fundamentach lochu lub geometrii poziomu
	if (!IsValidSurfaceTarget(HitActor))
	{
		return 0;
	}

	// 2. Wyliczenie orientacji powłoki powierzchniowej i namalowanie komórek
	const FVector SurfaceNormal = HitResult.ImpactNormal.IsNearlyZero() ? FVector::UpVector : HitResult.ImpactNormal.GetSafeNormal();

	return PaintSurface(
		HitResult.ImpactPoint,
		SurfaceNormal,
		Radius,
		Status,
		Duration,
		Instigator);
}

bool UDungeonSurfaceSubsystem::ApplyStatusToCell(
	const FSurfaceCellCoord& Coord,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator)
{
	if (!GetWorld() || IncomingStatus == EStatusEffectType::None || Duration <= 0.0f)
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	FSurfaceCellData* Existing = ActiveCells.Find(Coord);

	// 1. Pusta komórka: dodajemy nowy wpis do siatki
	if (!Existing || Existing->Status == EStatusEffectType::None)
	{
		FSurfaceCellData& NewCell = ActiveCells.FindOrAdd(Coord);
		NewCell.Status = IncomingStatus;
		NewCell.ServerEndTime = CurrentTime + Duration;
		NewCell.Instigator = Instigator;

		OnSurfaceCellChanged.Broadcast(Coord, IncomingStatus, Instigator);
		return true;
	}

	// 2. Identyczny żywioł: odświeżamy czas trwania i instigatora
	if (Existing->Status == IncomingStatus)
	{
		Existing->ServerEndTime = FMath::Max(Existing->ServerEndTime, CurrentTime + Duration);
		Existing->Instigator = Instigator;
		return true;
	}

	// 3. Różny żywioł: ewaluacja w centralnej bibliotece chemicznej
	const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(IncomingStatus, { Existing->Status });
	if (Reaction.bReactionOccurred)
	{
		if (Reaction.ResultingStatus != EStatusEffectType::None)
		{
			// Nowy status powstały w wyniku reakcji (np. Olej + Ogień -> Burning)
			const float NewDuration = (Reaction.ResultingDuration > 0.0f) ? Reaction.ResultingDuration : Duration;
			Existing->Status = Reaction.ResultingStatus;
			Existing->ServerEndTime = CurrentTime + NewDuration;
			Existing->Instigator = Instigator;
			OnSurfaceCellChanged.Broadcast(Coord, Reaction.ResultingStatus, Instigator);
			return true;
		}
		else if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
		{
			// Wzajemna neutralizacja / anihilacja (np. Woda gasi Ogień / Ogień odparowuje Wodę)
			OnSurfaceCellChanged.Broadcast(Coord, EStatusEffectType::None, Instigator);
			ActiveCells.Remove(Coord);
			return true;
		}
		else if (Reaction.bConsumeIncomingStatus)
		{
			// Przychodzący status uległ zużyciu, dotychczasowy stan komórki pozostaje
			return true;
		}
	}

	// 4. Brak dedykowanej reakcji: nowy status zastępuje poprzedni (np. wypieranie płynów)
	Existing->Status = IncomingStatus;
	Existing->ServerEndTime = CurrentTime + Duration;
	Existing->Instigator = Instigator;
	OnSurfaceCellChanged.Broadcast(Coord, IncomingStatus, Instigator);
	return true;
}

int32 UDungeonSurfaceSubsystem::PaintSurface(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator)
{
	TSet<FSurfaceCellCoord> ProcessedCoords;
	return PaintSurfaceInternal(HitLocation, HitNormal, Radius, Status, Duration, Instigator, &ProcessedCoords);
}

int32 UDungeonSurfaceSubsystem::PaintSurfaceInternal(
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	EStatusEffectType Status,
	float Duration,
	AActor* Instigator,
	TSet<FSurfaceCellCoord>* ProcessedCoords)
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
	const int32 StepRadius = (Radius <= SafeCellSize * 0.5f) ? 0 : FMath::CeilToInt(Radius / SafeCellSize);
	const float RadiusSq = FMath::Square(Radius);

	int32 AffectedCount = 0;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfacePaintValidation), false, Instigator);

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

			// 1. Line of Sight (LoS): Sprawdzamy, czy między punktem uderzenia a próbką nie ma przeszkody (filar, narożnik)
			if (!Offset.IsNearlyZero())
			{
				if (!HasSurfaceLineOfSight(GetWorld(), HitLocation, SamplePoint, Normal, TraceParams))
				{
					continue;
				}
			}

			// 2. Drop-Off Test: Sprawdzamy, czy pod próbką fizycznie istnieje architektura (brak wiszenia w powietrzu poza filarem)
			FHitResult SurfaceHit;
			if (!CheckSurfacePresenceAt(GetWorld(), SamplePoint, Normal, SurfaceHit, TraceParams))
			{
				continue;
			}

			const FSurfaceCellCoord Coord = FSurfaceCellCoord::FromWorldLocation(SurfaceHit.ImpactPoint, Normal, SafeCellSize);

			// Pomijamy koordynaty już przetworzone w tym samym złożonym zdarzeniu (np. wybuch wielopromieniowy)
			if (ProcessedCoords && ProcessedCoords->Contains(Coord))
			{
				continue;
			}

			if (ProcessedCoords)
			{
				ProcessedCoords->Add(Coord);
			}

			// JEDYNY PUNKT STYKU: ApplyStatusToCell decyduje o reakcji i stanie komórki
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator))
			{
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

	// 1. Zbiór komórek przetworzonych w ramach tego wybuchu (brak wyścigów i podwójnego malowania)
	TSet<FSurfaceCellCoord> ProcessedCoords;

	// 2. Bezpośrednia ewaluacja istniejących aktywnych komórek w sferze wybuchu z Line-of-Sight
	TArray<FSurfaceCellCoord> CellsInRadius;
	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FVector CellWorldPos = Coord.ToWorldLocation(SafeCellSize);
		if (FVector::DistSquared(Origin, CellWorldPos) <= RadiusSq)
		{
			// Line of Sight: czy fala wybuchu widzi tę komórkę bez przeszkód (np. filara lub narożnika ściany)?
			const FVector CellSurfacePos = CellWorldPos + SurfaceGridUtils::FaceDirectionToNormal(Coord.Face) * (SafeCellSize * 0.45f);
			FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(BurstCellLoS), false, Instigator);
			FHitResult LoSHit;
			if (GetWorld()->LineTraceSingleByChannel(LoSHit, Origin, CellSurfacePos, ECC_WorldStatic, LoSParams))
			{
				if (LoSHit.bBlockingHit && LoSHit.Distance < FVector::Dist(Origin, CellSurfacePos) - 15.0f)
				{
					// Sprawdzamy, czy uderzenie nie nastąpiło w samą płaszczyznę komórki (np. nierówności/pęknięcia podłogi)
					const bool bIsParallelSurface = FVector::DotProduct(LoSHit.ImpactNormal, SurfaceGridUtils::FaceDirectionToNormal(Coord.Face)) > 0.6f;
					if (!bIsParallelSurface)
					{
						// Przeszkoda poprzeczna (ściana, filar) zasłania tę komórkę przed falą wybuchu!
						continue;
					}
				}
			}

			CellsInRadius.Add(Coord);
		}
	}

	for (const FSurfaceCellCoord& Coord : CellsInRadius)
	{
		ProcessedCoords.Add(Coord);
		if (ApplyStatusToCell(Coord, Status, Duration, Instigator))
		{
			AffectedCount++;
		}
	}

	// 3. Wszechkierunkowa projekcja wybuchu na otaczające powierzchnie lochu (posadzka, sufit, ściany, rampy)
	TArray<FVector> ScanDirections;
	ScanDirections.Reserve(18);

	// 3a. Posadzka lochu (grawitacyjny opad / dolna strefa)
	ScanDirections.Add(FVector(0.0f, 0.0f, -1.0f));

	// 3b. Sufit lochu
	ScanDirections.Add(FVector(0.0f, 0.0f, 1.0f));

	// 3c. 8 kierunków horyzontalnych (pionowe ściany, filary)
	constexpr int32 NumHorizontal = 8;
	for (int32 i = 0; i < NumHorizontal; ++i)
	{
		const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * (360.0f / static_cast<float>(NumHorizontal)));
		ScanDirections.Add(FVector(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f));
	}

	// 3d. 4 kierunki skośne w dół (Pitch -35 deg) - rampy 30-45° i spadki terenu
	constexpr float PitchDown = -0.5736f;     // sin(-35 deg)
	constexpr float HorizScaleDown = 0.8192f; // cos(-35 deg)
	for (int32 i = 0; i < 4; ++i)
	{
		const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * 90.0f + 22.5f);
		ScanDirections.Add(FVector(FMath::Cos(AngleRad) * HorizScaleDown, FMath::Sin(AngleRad) * HorizScaleDown, PitchDown));
	}

	// 3e. 4 kierunki skośne w górę (Pitch +35 deg) - sklepienia łukowe i zadaszenia
	constexpr float PitchUp = 0.5736f;       // sin(35 deg)
	constexpr float HorizScaleUp = 0.8192f;  // cos(35 deg)
	for (int32 i = 0; i < 4; ++i)
	{
		const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * 90.0f + 22.5f);
		ScanDirections.Add(FVector(FMath::Cos(AngleRad) * HorizScaleUp, FMath::Sin(AngleRad) * HorizScaleUp, PitchUp));
	}

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfaceBurstTrace), false, Instigator);
	if (Instigator)
	{
		TraceParams.AddIgnoredActor(Instigator);
	}

	struct FPaintedSurfaceRecord
	{
		FVector ImpactPoint;
		FVector ImpactNormal;
		float Radius;
	};
	TArray<FPaintedSurfaceRecord> PaintedPlanes;

	for (const FVector& RayDir : ScanDirections)
	{
		// Dla podłogi dajemy lekki margines zasięgu (wybuch beczki może być na Z+50)
		const float TraceDist = (RayDir.Z < -0.9f) ? (Radius + 100.0f) : Radius;
		const FVector TraceEnd = Origin + RayDir * TraceDist;

		FHitResult SurfaceHit;
		if (GetWorld()->LineTraceSingleByChannel(SurfaceHit, Origin, TraceEnd, ECC_Visibility, TraceParams))
		{
			AActor* HitActor = SurfaceHit.GetActor();
			if (!HitActor || !IsValidSurfaceTarget(HitActor))
			{
				continue;
			}

			const float DistToSurface = FMath::Clamp(SurfaceHit.Distance, 0.0f, Radius);
			const float BaseDiscRadius = FMath::Sqrt(FMath::Max(0.0f, RadiusSq - FMath::Square(DistToSurface)));
			const float SurfaceSplashRadius = (RayDir.Z < -0.9f) ? (Radius * 0.75f) : FMath::Clamp(BaseDiscRadius * 0.75f, SafeCellSize * 0.5f, Radius);

			bool bAlreadyCovered = false;
			for (const FPaintedSurfaceRecord& Existing : PaintedPlanes)
			{
				const float NormalDot = FVector::DotProduct(SurfaceHit.ImpactNormal, Existing.ImpactNormal);
				const float PlaneDist = FMath::Abs(FVector::DotProduct(SurfaceHit.ImpactPoint - Existing.ImpactPoint, Existing.ImpactNormal));
				const float DistSq = FVector::DistSquared(SurfaceHit.ImpactPoint, Existing.ImpactPoint);

				if (NormalDot > 0.90f && PlaneDist < 25.0f && DistSq < FMath::Square(FMath::Max(Existing.Radius, SurfaceSplashRadius) * 0.85f))
				{
					bAlreadyCovered = true;
					break;
				}
			}

			if (bAlreadyCovered)
			{
				continue;
			}

			AffectedCount += PaintSurfaceInternal(
				SurfaceHit.ImpactPoint,
				SurfaceHit.ImpactNormal,
				SurfaceSplashRadius,
				Status,
				Duration,
				Instigator,
				&ProcessedCoords);

			PaintedPlanes.Add({ SurfaceHit.ImpactPoint, SurfaceHit.ImpactNormal, SurfaceSplashRadius });
		}
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

	// Aplikacja zebranych rozprzestrzenień żywiołów przez jednolity punkt styku
	for (const auto& PendingPair : PendingSpreads)
	{
		const FPendingSpreadCell& Pending = PendingPair.Value;
		ApplyStatusToCell(Pending.Coord, Pending.NewStatus, Pending.Duration, Pending.Instigator.Get());
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
						// Zadawanie natychmiastowych obrażeń reakcji (np. wybuch oleju pod stopami postaci)
						if (Reaction.BonusInstantDamage > 0.0f)
						{
							if (UDamageableComponent* Damageable = Pawn->FindComponentByClass<UDamageableComponent>())
							{
								Damageable->ApplyDamage(Reaction.BonusInstantDamage);
							}
						}

						// Jeśli status postaci uległ zużyciu w reakcji (np. woda na postaci odparowała przy gaszeniu ognia)
						if (Reaction.bConsumeIncomingStatus && StatusComp)
						{
							StatusComp->RemoveStatus(PawnStatus);
						}

						ApplyStatusToCell(CellCoord, PawnStatus, (Reaction.ResultingDuration > 0.0f ? Reaction.ResultingDuration : 5.0f), Pawn);
						break;
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

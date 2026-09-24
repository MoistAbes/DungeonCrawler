#include "DungeonSurfaceSubsystem.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "Engine/Brush.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

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

		TArray<FHitResult> Hits;
		if (World->LineTraceMultiByChannel(Hits, ProbeStart, ProbeEnd, ECC_WorldStatic, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetActor() && UDungeonSurfaceSubsystem::IsValidSurfaceTarget(Hit.GetActor()))
				{
					const float NormalDot = FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal);
					if (NormalDot > 0.65f)
					{
						const float DistFromPlane = FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - SamplePoint, SurfaceNormal));
						if (DistFromPlane < 25.0f)
						{
							OutHit = Hit;
							return true;
						}
					}
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

	// Wyliczenie orientacji powłoki powierzchniowej i namalowanie komórek
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
	AActor* Instigator,
	EPhysicalMaterialType ExplicitMaterial)
{
	if (!GetWorld() || IncomingStatus == EStatusEffectType::None || Duration <= 0.0f)
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	FSurfaceCellData* Existing = ActiveCells.Find(Coord);

	// 1. Pusta komórka: dodajemy nowy wpis do siatki
	if (!Existing || Existing->IsEmpty())
	{
		// Ustalamy materiał powierzchni dla nowej komórki.
		// Jeśli ExplicitMaterial to Stone (wartość domyślna), pobieramy tożsamość materiałową z geometrii lochu pod koordynatem.
		EPhysicalMaterialType SurfaceMat = ExplicitMaterial;
		if (SurfaceMat == EPhysicalMaterialType::Stone)
		{
			SurfaceMat = GetSurfaceMaterialAtCoord(Coord);
		}

		const bool bCanReceive = UElementalReactionRules::CanMaterialReceiveStatus(SurfaceMat, IncomingStatus, {});
		UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyStatusToCell -> NEW Coord(%d,%d,%d Face:%d) | Status:%s | ExplicitMat:%s | ResolvedMat:%s | CanReceive:%d | Instigator:%s"),
			Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face),
			*UEnum::GetValueAsString(IncomingStatus),
			*UEnum::GetValueAsString(ExplicitMaterial),
			*UEnum::GetValueAsString(SurfaceMat),
			bCanReceive,
			*GetNameSafe(Instigator));

		// Sprawdzamy czy materiał powierzchni może przyjąć ten status (np. kamień nie przyjmie czystego prądu ani ognia)
		if (!bCanReceive)
		{
			return false;
		}

		FSurfaceCellData& NewCell = ActiveCells.FindOrAdd(Coord);
		NewCell.ActiveStatuses.Reset();
		NewCell.SurfaceMaterial = SurfaceMat;
		NewCell.ActiveStatuses.Add({ IncomingStatus, CurrentTime + Duration, Instigator });

		OnSurfaceCellChanged.Broadcast(Coord, IncomingStatus, Instigator);
		return true;
	}

	// 2. Identyczny żywioł: odświeżamy czas trwania i instigatora
	if (FSurfaceCellStatusEntry* ExistingEntry = Existing->FindStatus(IncomingStatus))
	{
		ExistingEntry->ServerEndTime = FMath::Max(ExistingEntry->ServerEndTime, CurrentTime + Duration);
		ExistingEntry->Instigator = Instigator;
		return true;
	}

	// 3. Różny żywioł: ewaluacja w centralnych regułach reakcji chemicznych
	const FElementalReactionResult Reaction = UElementalReactionRules::EvaluateReaction(IncomingStatus, Existing->GetStatusTypes());
	UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyStatusToCell -> REACTION EVAL Coord(%d,%d,%d Face:%d) | Incoming:%s | ExistingMat:%s | ReactionOccurred:%d | ResultingStatus:%s"),
		Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face),
		*UEnum::GetValueAsString(IncomingStatus),
		*UEnum::GetValueAsString(Existing->SurfaceMaterial),
		Reaction.bReactionOccurred,
		*UEnum::GetValueAsString(Reaction.ResultingStatus));
	if (Reaction.bReactionOccurred)
	{
		bool bModified = false;
		const TArray<EStatusEffectType> ActiveStatusesBefore = Existing->GetStatusTypes();

		// A. Dodanie lub odświeżenie statusu wynikowego reakcji (np. Olej + Ogień -> Burning)
		// Sprawdzamy CanMaterialReceiveStatus z listą statusów PRZED usunięciem (np. kamień z olejem dopuszcza Burning przez BypassTraitsIfActive)
		if (Reaction.ResultingStatus != EStatusEffectType::None)
		{
			if (UElementalReactionRules::CanMaterialReceiveStatus(Existing->SurfaceMaterial, Reaction.ResultingStatus, ActiveStatusesBefore))
			{
				const float NewDuration = (Reaction.ResultingDuration > 0.0f) ? Reaction.ResultingDuration : Duration;
				if (FSurfaceCellStatusEntry* ResEntry = Existing->FindStatus(Reaction.ResultingStatus))
				{
					ResEntry->ServerEndTime = FMath::Max(ResEntry->ServerEndTime, CurrentTime + NewDuration);
					ResEntry->Instigator = Instigator;
				}
				else
				{
					Existing->ActiveStatuses.Add({ Reaction.ResultingStatus, CurrentTime + NewDuration, Instigator });
				}
				bModified = true;
			}
		}
		// B. Jeśli przychodzący status nie został skonsumowany w reakcji (np. prąd w wodzie: Conductive Shock)
		else if (!Reaction.bConsumeIncomingStatus)
		{
			if (!Existing->HasStatus(IncomingStatus))
			{
				if (UElementalReactionRules::CanMaterialReceiveStatus(Existing->SurfaceMaterial, IncomingStatus, ActiveStatusesBefore))
				{
					Existing->ActiveStatuses.Add({ IncomingStatus, CurrentTime + Duration, Instigator });
					bModified = true;
				}
			}
		}

		// C. Usunięcie wygaszonego/zutylizowanego w reakcji dotychczasowego statusu (np. spalonego oleju lub odparowanej wody)
		if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
		{
			Existing->RemoveStatus(Reaction.ExistingStatusToRemove);
			bModified = true;

			// D. WERYFIKACJA OSIEROCONYCH STATUSÓW (Orphaned Dependent Status Cleanup):
			// Jeśli usunięto status nośnika (np. Wet został wyparty przez Olej lub odparowany przez Ogień),
			// sprawdzamy czy pozostałe dotychczasowe statusy pasożytnicze (np. Electrified) mogą nadal legalnie istnieć
			// na tym materiale. Status wynikowy reakcji (ResultingStatus) został już zwalidowany w kroku A i nie jest osierocony.
			for (int32 Index = Existing->ActiveStatuses.Num() - 1; Index >= 0; --Index)
			{
				const EStatusEffectType RemainingStatus = Existing->ActiveStatuses[Index].Status;
				if (RemainingStatus == Reaction.ResultingStatus)
				{
					continue;
				}

				if (!UElementalReactionRules::CanMaterialReceiveStatus(Existing->SurfaceMaterial, RemainingStatus, Existing->GetStatusTypes()))
				{
					UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyStatusToCell -> Evicting orphaned dependent status %s from Coord(%d,%d,%d Face:%d) on material %s"),
						*UEnum::GetValueAsString(RemainingStatus), Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face),
						*UEnum::GetValueAsString(Existing->SurfaceMaterial));
					Existing->ActiveStatuses.RemoveAt(Index);
					bModified = true;
				}
			}
		}

		// Jeśli komórka po reakcji stała się pusta (np. neutralizacja Woda + Ogień -> para)
		if (Existing->IsEmpty())
		{
			OnSurfaceCellChanged.Broadcast(Coord, EStatusEffectType::None, Instigator);
			ActiveCells.Remove(Coord);
			return true;
		}

		if (bModified)
		{
			OnSurfaceCellChanged.Broadcast(Coord, Existing->GetDominantStatus(), Existing->GetDominantInstigator());
			return true;
		}

		return false;
	}

	// 4. Brak reakcji: jeśli nie wystąpił konflikt ani reakcja, sprawdzamy czy materiał pozwala na koegzystencję
	if (!Existing->HasStatus(IncomingStatus))
	{
		if (UElementalReactionRules::CanMaterialReceiveStatus(Existing->SurfaceMaterial, IncomingStatus, Existing->GetStatusTypes()))
		{
			Existing->ActiveStatuses.Add({ IncomingStatus, CurrentTime + Duration, Instigator });
			OnSurfaceCellChanged.Broadcast(Coord, Existing->GetDominantStatus(), Existing->GetDominantInstigator());
			return true;
		}
	}

	return false;
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

			// Rozpoznanie tożsamości materiałowej trafionego elementu architektury
			EPhysicalMaterialType HitMat = EPhysicalMaterialType::Stone;
			if (AActor* HitActor = SurfaceHit.GetActor())
			{
				if (HitActor->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
				{
					HitMat = IMaterialProviderInterface::Execute_GetMaterialType(HitActor);
				}
			}

			// JEDYNY PUNKT STYKU: ApplyStatusToCell decyduje o reakcji i stanie komórki
			if (ApplyStatusToCell(Coord, Status, Duration, Instigator, HitMat))
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

EPhysicalMaterialType UDungeonSurfaceSubsystem::GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return EPhysicalMaterialType::Stone;
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
	const FVector Center = Coord.ToWorldLocation(SafeCellSize);
	const FVector ProbeStart = Center + Normal * 20.0f;
	const FVector ProbeEnd = Center - Normal * 30.0f;

	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SurfaceMatQuery), false);
	if (World->LineTraceMultiByChannel(Hits, ProbeStart, ProbeEnd, ECC_WorldStatic, Params))
	{
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (HitActor && UDungeonSurfaceSubsystem::IsValidSurfaceTarget(HitActor))
			{
				if (HitActor->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
				{
					const EPhysicalMaterialType FoundMat = IMaterialProviderInterface::Execute_GetMaterialType(HitActor);
					UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] GetSurfaceMaterialAtCoord(%d,%d,%d Face:%d) -> HitActor: %s | Material: %s"),
						Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face), *HitActor->GetName(), *UEnum::GetValueAsString(FoundMat));
					return FoundMat;
				}
				else
				{
					UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] GetSurfaceMaterialAtCoord(%d,%d,%d Face:%d) -> HitActor %s DOES NOT implement IMaterialProviderInterface! Defaulting to Stone"),
						Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face), *HitActor->GetName());
					return EPhysicalMaterialType::Stone;
				}
			}
		}
	}
	else
	{
		UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] GetSurfaceMaterialAtCoord(%d,%d,%d Face:%d) -> LineTrace MISSED geometry! (Start:%s, End:%s)"),
			Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face), *ProbeStart.ToString(), *ProbeEnd.ToString());
	}

	return EPhysicalMaterialType::Stone;
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

	UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyElementalBurst START -> Origin: %s, Radius: %.1f, Status: %s"),
		*Origin.ToString(), Radius, *UEnum::GetValueAsString(Status));

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

	UE_LOG(LogDungeonElements, Warning, TEXT("[SurfaceGrid] ApplyElementalBurst FINISH -> AffectedCount: %d"), AffectedCount);
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
	for (auto It = ActiveCells.CreateIterator(); It; ++It)
	{
		FSurfaceCellData& Cell = It.Value();
		bool bStatusRemoved = false;
		for (int32 Index = Cell.ActiveStatuses.Num() - 1; Index >= 0; --Index)
		{
			if (Cell.ActiveStatuses[Index].ServerEndTime > 0.0f && CurrentTime >= Cell.ActiveStatuses[Index].ServerEndTime)
			{
				Cell.ActiveStatuses.RemoveAt(Index);
				bStatusRemoved = true;
			}
		}

		// Jeśli wygasł status nośnika (np. Wet odparowało lub woda wyschła),
		// sprawdzamy czy pozostałe w komórce statusy pasożytnicze (np. Electrified) mogą nadal istnieć na tym materiale
		if (bStatusRemoved && !Cell.IsEmpty())
		{
			for (int32 Index = Cell.ActiveStatuses.Num() - 1; Index >= 0; --Index)
			{
				const EStatusEffectType RemainingStatus = Cell.ActiveStatuses[Index].Status;
				if (!UElementalReactionRules::CanMaterialReceiveStatus(Cell.SurfaceMaterial, RemainingStatus, Cell.GetStatusTypes()))
				{
					Cell.ActiveStatuses.RemoveAt(Index);
				}
			}
		}

		if (Cell.IsEmpty())
		{
			OnSurfaceCellChanged.Broadcast(It.Key(), EStatusEffectType::None, nullptr);
			It.RemoveCurrent();
		}
		else if (bStatusRemoved)
		{
			OnSurfaceCellChanged.Broadcast(It.Key(), Cell.GetDominantStatus(), Cell.GetDominantInstigator());
		}
	}

	// 2. Propagacja żywiołów na sąsiednie komórki (Cellular Automata)
	// Wyłącznie reguły z UElementalReactionRules - zero twardego kodowania statusów w podsystemie
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

		if (SourceData.IsEmpty())
		{
			continue;
		}

		SourceCoord.GetAdjacentNeighbors(NeighborCoords);

		for (const FSurfaceCellCoord& NeighborCoord : NeighborCoords)
		{
			if (const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord))
			{
				if (NeighborData->IsEmpty())
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

				// Sprawdzamy każdy status ze źródła z każdym statusem sąsiada
				for (const FSurfaceCellStatusEntry& SourceEntry : SourceData.ActiveStatuses)
				{
					for (const FSurfaceCellStatusEntry& NeighborEntry : NeighborData->ActiveStatuses)
					{
						if (SourceEntry.Status == NeighborEntry.Status)
						{
							continue;
						}

						FElementalReactionResult SpreadReaction;
						if (UElementalReactionRules::CanSpreadToNeighbor(SourceEntry.Status, NeighborEntry.Status, SpreadReaction))
						{
							const EStatusEffectType TargetStatus = (SpreadReaction.ResultingStatus != EStatusEffectType::None) ? SpreadReaction.ResultingStatus : SourceEntry.Status;

							// Bezpiecznik fizyczny: jeśli sąsiad ma już ten status, nie rozprzestrzeniaj go ponownie (zapobiega nieskończonym pętlom)
							if (NeighborData->HasStatus(TargetStatus))
							{
								continue;
							}

							const float RemainingSourceTime = SourceEntry.ServerEndTime - CurrentTime;
							if (RemainingSourceTime <= 0.1f)
							{
								continue;
							}

							// Jeśli prąd rozchodzi się po wodzie, synchronizujemy czas trwania z pozostałym czasem źródła,
							// aby cała kałuża gasła jednocześnie i nie tworzyła zapętlenia (bounce-back).
							const float CalculatedDuration = (SourceEntry.Status == EStatusEffectType::Electrified && RemainingSourceTime > 0.0f)
								? FMath::Max(0.5f, RemainingSourceTime)
								: ((SpreadReaction.ResultingDuration > 0.0f) ? SpreadReaction.ResultingDuration : 5.0f);

							if (FPendingSpreadCell* ExistingPending = PendingSpreads.Find(NeighborCoord))
							{
								ExistingPending->Duration = FMath::Max(ExistingPending->Duration, CalculatedDuration);
							}
							else
							{
								FPendingSpreadCell& Pending = PendingSpreads.Add(NeighborCoord);
								Pending.Coord = NeighborCoord;
								Pending.NewStatus = TargetStatus;
								Pending.Duration = CalculatedDuration;
								Pending.Instigator = SourceEntry.Instigator;
							}
						}
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

	// 3. Server-Authoritative: dwukierunkowa interakcja żywiołowa między obiektami a komórkami
	if (World->GetNetMode() != NM_Client && ActiveCells.Num() > 0)
	{
		// Pomocnicza funkcja do obsługi interakcji dowolnego aktora ze StatusEffectComponent z siatką komórek
		auto ProcessActorInteraction = [this, CurrentTime](AActor* Actor, UStatusEffectComponent* StatusComp)
		{
			if (!Actor || Actor->IsActorBeingDestroyed() || !StatusComp)
			{
				return;
			}

			TArray<FSurfaceCellCoord> TouchedCells;
			GetCellsTouchingActor(Actor, TouchedCells);
			if (TouchedCells.Num() == 0)
			{
				return;
			}

			TArray<EStatusEffectType> ActorStatuses = StatusComp->GetActiveStatuses();
			UElementalReactionRules::SortByReactionPriority(ActorStatuses);

			TMap<EStatusEffectType, TWeakObjectPtr<AActor>> FloorStatusesToApply;

			for (const FSurfaceCellCoord& CellCoord : TouchedCells)
			{
				FSurfaceCellData* CellData = ActiveCells.Find(CellCoord);
				if (!CellData || CellData->IsEmpty())
				{
					continue;
				}

				// A. Interakcja Obiekt -> Komórka (z zachowaniem priorytetu reakcji i braku fałszywego break)
				for (int32 StatusIdx = ActorStatuses.Num() - 1; StatusIdx >= 0; --StatusIdx)
				{
					const EStatusEffectType ActorStatus = ActorStatuses[StatusIdx];
					if (ActorStatus == EStatusEffectType::None)
					{
						continue;
					}

					// Czy komórka nadal istnieje i ma z czym reagować?
					CellData = ActiveCells.Find(CellCoord);
					if (!CellData || CellData->IsEmpty())
					{
						break;
					}

					const FElementalReactionResult Reaction = UElementalReactionRules::EvaluateReaction(ActorStatus, CellData->GetStatusTypes());
					if (Reaction.bReactionOccurred)
					{
						// Obiekt swoją obecnością NIE wypiera cieczy na posadzce
						// (np. mokre buty gracza lub mokra beczka nie zamieniają kałuży oleju w wodę)
						if (Reaction.ReactionTag == FName(TEXT("Liquid_Displaced")))
						{
							continue;
						}

						// Zadawanie natychmiastowych obrażeń reakcji (np. wybuch oleju, szok przewodzenia)
						if (Reaction.BonusInstantDamage > 0.0f)
						{
							if (UDamageableComponent* Damageable = Actor->FindComponentByClass<UDamageableComponent>())
							{
								Damageable->ApplyDamage(Reaction.BonusInstantDamage);
							}
						}

						// Jeśli status obiektu uległ zużyciu w reakcji (np. woda na obiekcie odparowała przy gaszeniu ognia)
						if (Reaction.bConsumeIncomingStatus)
						{
							StatusComp->RemoveStatus(ActorStatus);
							ActorStatuses.RemoveAt(StatusIdx);
						}

						ApplyStatusToCell(CellCoord, ActorStatus, (Reaction.ResultingDuration > 0.0f ? Reaction.ResultingDuration : 5.0f), Actor);
					}
				}

				// Zbieranie statusów z tej komórki do unikalnego zbioru dla obiektu
				if (FSurfaceCellData* CurrentCell = ActiveCells.Find(CellCoord))
				{
					if (!CurrentCell->IsEmpty())
					{
						for (const FSurfaceCellStatusEntry& Entry : CurrentCell->ActiveStatuses)
						{
							if (!FloorStatusesToApply.Contains(Entry.Status))
							{
								UE_LOG(LogDungeonElements, Warning, TEXT("[ProcessGridTick] Actor %s TOUCHING active cell Coord(%d,%d,%d Face:%d) | Status:%s | CellMat:%s | TimeLeft:%.1fs"),
									*Actor->GetName(), CellCoord.X, CellCoord.Y, CellCoord.Z, static_cast<int32>(CellCoord.Face),
									*UEnum::GetValueAsString(Entry.Status),
									*UEnum::GetValueAsString(CurrentCell->SurfaceMaterial),
									Entry.ServerEndTime - CurrentTime);
								FloorStatusesToApply.Add(Entry.Status, Entry.Instigator);
							}
						}
					}
				}
			}

			// B. Interakcja Komórka -> Obiekt: aplikacja każdego unikalnego statusu z posadzki tylko RAZ na obiekt na tick
			for (const auto& StatusPair : FloorStatusesToApply)
			{
				UE_LOG(LogDungeonElements, Log, TEXT("[ProcessGridTick] Applying floor status %s to %s"),
					*UEnum::GetValueAsString(StatusPair.Key), *Actor->GetName());
				StatusComp->ApplyStatus(StatusPair.Key, 1.5f, StatusPair.Value.Get());
			}
		};

		// 1. Jeśli zarejestrowano komponenty statusów, iterujemy wyłącznie po nich (optymalna ścieżka O(N))
		if (RegisteredStatusComponents.Num() > 0)
		{
			for (int32 Idx = RegisteredStatusComponents.Num() - 1; Idx >= 0; --Idx)
			{
				UStatusEffectComponent* StatusComp = RegisteredStatusComponents[Idx].Get();
				if (!StatusComp || !IsValid(StatusComp))
				{
					RegisteredStatusComponents.RemoveAt(Idx);
					continue;
				}

				AActor* OwnerActor = StatusComp->GetOwner();
				ProcessActorInteraction(OwnerActor, StatusComp);
			}
		}
		else
		{
			// 2. Fallback (np. w trakcie Live Coding zanim nowe obiekty wywołają BeginPlay):
			// Przetwarzanie pionów oraz interaktywnych rekwizytów
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				if (APawn* Pawn = *It)
				{
					if (UStatusEffectComponent* StatusComp = Pawn->FindComponentByClass<UStatusEffectComponent>())
					{
						ProcessActorInteraction(Pawn, StatusComp);
					}
				}
			}
			for (TActorIterator<AInteractivePropBase> It(World); It; ++It)
			{
				if (AInteractivePropBase* Prop = *It)
				{
					if (UStatusEffectComponent* StatusComp = Prop->FindComponentByClass<UStatusEffectComponent>())
					{
						ProcessActorInteraction(Prop, StatusComp);
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

#include "DungeonSurfaceSubsystem.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "Engine/Brush.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

namespace
{
	/** Rozpoznaje tożsamość materiałową aktora lochu z bezpiecznym fallbackiem do Stone */
	static EPhysicalMaterialType GetMaterialFromActor(const AActor* Actor)
	{
		if (Actor && Actor->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
		{
			return IMaterialProviderInterface::Execute_GetMaterialType(Actor);
		}
		return EPhysicalMaterialType::Stone;
	}

	/** Uniwersalny próbnik powierzchni: weryfikuje geometrię architektury i zwraca materiał */
	static bool ProbeSurfaceAt(
		const UWorld* World,
		const FVector& ProbeLocation,
		const FVector& SurfaceNormal,
		float ProbeDistance,
		FHitResult& OutHit,
		EPhysicalMaterialType& OutMaterial,
		const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	{
		OutMaterial = EPhysicalMaterialType::Stone;
		if (!World)
		{
			return false;
		}

		const FVector ProbeStart = ProbeLocation + SurfaceNormal * 20.0f;
		const FVector ProbeEnd = ProbeLocation - SurfaceNormal * ProbeDistance;

		TArray<FHitResult> Hits;
		const FCollisionObjectQueryParams ObjectParams(ECC_WorldStatic);
		if (World->LineTraceMultiByObjectType(Hits, ProbeStart, ProbeEnd, ObjectParams, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				AActor* HitActor = Hit.GetActor();
				if (HitActor && UDungeonSurfaceSubsystem::IsValidSurfaceTarget(HitActor))
				{
					if (FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal) > 0.4f)
					{
						OutHit = Hit;
						OutMaterial = GetMaterialFromActor(HitActor);
						return true;
					}
				}
			}
		}
		return false;
	}

	/** Sprawdza, czy w danym punkcie fizycznie istnieje płaszczyzna architektury (Drop-off test na krawędziach filarów i ścian) */
	static bool CheckSurfacePresenceAt(
		const UWorld* World,
		const FVector& SamplePoint,
		const FVector& SurfaceNormal,
		FHitResult& OutHit,
		const FCollisionQueryParams& Params)
	{
		EPhysicalMaterialType IgnoredMat;
		return ProbeSurfaceAt(World, SamplePoint, SurfaceNormal, 30.0f, OutHit, IgnoredMat, Params);
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

	/** Wyznacza wektory styczne płaszczyzny dla zadanego kierunku ściany lub podłogi */
	static void GetFaceTangents(ESurfaceFaceDirection Face, FVector& OutTangentU, FVector& OutTangentV)
	{
		switch (Face)
		{
		case ESurfaceFaceDirection::Up:
		case ESurfaceFaceDirection::Down:
			OutTangentU = FVector(1.0f, 0.0f, 0.0f);
			OutTangentV = FVector(0.0f, 1.0f, 0.0f);
			break;
		case ESurfaceFaceDirection::North:
		case ESurfaceFaceDirection::South:
			OutTangentU = FVector(0.0f, 1.0f, 0.0f);
			OutTangentV = FVector(0.0f, 0.0f, 1.0f);
			break;
		case ESurfaceFaceDirection::East:
		case ESurfaceFaceDirection::West:
		default:
			OutTangentU = FVector(1.0f, 0.0f, 0.0f);
			OutTangentV = FVector(0.0f, 0.0f, 1.0f);
			break;
		}
	}

	/** Zwraca prekomputowane 18 kierunków skanowania wybuchu żywiołowego w 3D */
	static const TArray<FVector>& GetBurstScanDirections()
	{
		static const TArray<FVector> ScanDirections = []()
		{
			TArray<FVector> Dirs;
			Dirs.Reserve(18);

			// Posadzka (dolna strefa) & Sufit
			Dirs.Add(FVector(0.0f, 0.0f, -1.0f));
			Dirs.Add(FVector(0.0f, 0.0f, 1.0f));

			// 8 kierunków horyzontalnych (ściany, filary)
			constexpr int32 NumHorizontal = 8;
			for (int32 i = 0; i < NumHorizontal; ++i)
			{
				const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * (360.0f / static_cast<float>(NumHorizontal)));
				Dirs.Add(FVector(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f));
			}

			// 4 kierunki skośne w dół (Pitch -35 deg)
			constexpr float PitchDown = -0.573576436f;
			constexpr float HorizScaleDown = 0.819152044f;
			for (int32 i = 0; i < 4; ++i)
			{
				const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * 90.0f + 22.5f);
				Dirs.Add(FVector(FMath::Cos(AngleRad) * HorizScaleDown, FMath::Sin(AngleRad) * HorizScaleDown, PitchDown));
			}

			// 4 kierunki skośne w górę (Pitch +35 deg)
			constexpr float PitchUp = 0.573576436f;
			constexpr float HorizScaleUp = 0.819152044f;
			for (int32 i = 0; i < 4; ++i)
			{
				const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * 90.0f + 22.5f);
				Dirs.Add(FVector(FMath::Cos(AngleRad) * HorizScaleUp, FMath::Sin(AngleRad) * HorizScaleUp, PitchUp));
			}

			return Dirs;
		}();
		return ScanDirections;
	}

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
		const float RemainingSourceTime = SourceEntry.ServerEndTime - CurrentTime;
		if (RemainingSourceTime <= 0.1f)
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
					const float NeighborCarrierRemaining = NeighborEntry.ServerEndTime - CurrentTime;
					if (NeighborCarrierRemaining > 0.0f)
					{
						CalculatedDuration = NeighborCarrierRemaining;
					}
				}
				else if (RemainingSourceTime > 0.0f)
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
	if (!Actor || !IsValid(Actor) || Actor->IsActorBeingDestroyed())
	{
		return false;
	}

	// 1. Wykluczamy postacie oraz dynamiczne/interaktywne rekwizyty lochu (beczki, skrzynie)
	if (Actor->IsA<APawn>() || Actor->IsA<AInteractivePropBase>())
	{
		return false;
	}

	// 2. Akceptujemy oficjalne fundamenty i architekturę lochu (ściany, podłogi, sufity)
	if (const ADungeonStructureBase* Structure = Cast<ADungeonStructureBase>(Actor))
	{
		// Tylko zniszczalne elementy architektury lochu mogą przestać być powierzchnią po zniszczeniu
		if (Structure->IsDestructible())
		{
			if (const UDamageableComponent* DmgComp = Structure->GetDamageableComponent())
			{
				if (DmgComp->IsDestroyed())
				{
					return false;
				}
			}
		}
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

	// Jeśli komórka nie istnieje w siatce, ustalamy tożsamość materiałową podłoża
	// oraz upewniamy się, że pod komórką faktycznie istnieje fizyczna architektura lochu.
	EPhysicalMaterialType SurfaceMat = ExplicitMaterial;
	if (!Existing)
	{
		if (!GetSurfaceMaterialAtCoord(Coord, SurfaceMat))
		{
			UE_LOG(LogDungeonElements, Verbose, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) rejected: No valid surface geometry present!"),
				Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face));
			return false;
		}
	}
	else
	{
		SurfaceMat = Existing->SurfaceMaterial;
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

	// Cała chemia, reakcje, nośniki i wygaszanie są liczone w centralnym silniku zasad (UElementalReactionRules).
	// Komórka jedynie odbiera i utrwala nowy stan.
	const FSurfaceCellTransitionResult Result = UElementalReactionRules::CalculateCellTransition(
		CellData,
		IncomingStatus,
		Duration,
		Instigator,
		CurrentTime);

	if (!Result.bAccepted)
	{
		return false;
	}

	// Obsługa ewentualnego wybuchu / obrażeń z reakcji
	if (Result.Reaction.bReactionOccurred && Result.Reaction.BonusInstantDamage > 0.0f)
	{
		const FVector CellCenter = Coord.ToWorldLocation(CellSize);
		UKineticForceLibrary::ApplyExplosion(
			this,
			CellCenter,
			CellSize * 1.5f,
			Result.Reaction.BonusInstantDamage,
			800.0f,
			Instigator,
			nullptr,
			false);

		// Jeśli wybuch zniszczył strukturę podtrzymującą tę komórkę, przerywamy i nie zapisujemy stanu w siatce!
		EPhysicalMaterialType PostExplosionMat;
		if (!GetSurfaceMaterialAtCoord(Coord, PostExplosionMat))
		{
			UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) structure was destroyed by reaction explosion! Aborting state save."),
				Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face));
			ActiveCells.Remove(Coord);
			OnSurfaceCellChanged.Broadcast(Coord, EStatusEffectType::None, Instigator);
			return true;
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

	// Ostateczny bezpiecznik fizyczny: upewniamy się, że pod komórką nadal istnieje nienaruszona architektura lochu.
	// Zapobiega zapisaniu lewitującej komórki w siatce, jeśli struktura została zniszczona w trakcie ewaluacji.
	EPhysicalMaterialType FinalCheckMat;
	if (!GetSurfaceMaterialAtCoord(Coord, FinalCheckMat))
	{
		UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) aborted: Surface geometry is destroyed or absent!"),
			Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face));
		ActiveCells.Remove(Coord);
		OnSurfaceCellChanged.Broadcast(Coord, EStatusEffectType::None, Instigator);
		return false;
	}

	// Zapisanie nowego stanu komórki
	ActiveCells.Add(Coord, CellData);

	if (Result.bStateModified)
	{
		FString StatusesStr;
		for (const auto& St : CellData.ActiveStatuses)
		{
			StatusesStr += FString::Printf(TEXT("[%s: %.1fs] "), *UEnum::GetValueAsString(St.Status), St.ServerEndTime - CurrentTime);
		}
		UE_LOG(LogDungeonElements, Log, TEXT("[SurfaceGrid] ApplyStatusToCell Coord(%d,%d,%d Face:%d) Updated -> %s"),
			Coord.X, Coord.Y, Coord.Z, static_cast<int32>(Coord.Face), *StatusesStr);
		OnSurfaceCellChanged.Broadcast(Coord, CellData.GetDominantStatus(), CellData.GetDominantInstigator());
	}

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
	return PaintSurfaceInternal(HitLocation, HitNormal, Radius, Status, Duration, Instigator, nullptr);
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
	GetFaceTangents(FaceDir, TangentU, TangentV);

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
			if (!Offset.IsNearlyZero() && !HasSurfaceLineOfSight(GetWorld(), HitLocation, SamplePoint, Normal, TraceParams))
			{
				continue;
			}

			// 2. Drop-Off Test: Sprawdzamy, czy pod próbką fizycznie istnieje architektura (brak wiszenia w powietrzu poza filarem)
			FHitResult SurfaceHit;
			if (!CheckSurfacePresenceAt(GetWorld(), SamplePoint, Normal, SurfaceHit, TraceParams))
			{
				continue;
			}

			const FSurfaceCellCoord Coord = FSurfaceCellCoord::FromWorldLocation(SurfaceHit.ImpactPoint, Normal, SafeCellSize);

			// Pomijamy koordynaty już przetworzone w tym samym złożonym zdarzeniu (np. wybuch wielopromieniowy)
			if (ProcessedCoords)
			{
				if (ProcessedCoords->Contains(Coord))
				{
					continue;
				}
				ProcessedCoords->Add(Coord);
			}

			// Rozpoznanie tożsamości materiałowej trafionego elementu architektury
			const EPhysicalMaterialType HitMat = GetMaterialFromActor(SurfaceHit.GetActor());

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

void UDungeonSurfaceSubsystem::GetCellsTouchingActor(const AActor* Actor, TArray<FSurfaceCellCoord>& OutCoords) const
{
	OutCoords.Reset();

	if (!Actor || ActiveCells.Num() == 0)
	{
		return;
	}

	// Pełna bryła kolizyjna 3D (kapsuła gracza, mesh potwora lub skrzynki/kamienia)
	FBox ActorBox;
	if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		ActorBox = RootPrim->Bounds.GetBox();
	}
	else
	{
		ActorBox = Actor->GetComponentsBoundingBox(true);
	}

	if (!ActorBox.IsValid)
	{
		const FVector Loc = Actor->GetActorLocation();
		ActorBox = FBox(Loc - FVector(34.0f, 34.0f, 88.0f), Loc + FVector(34.0f, 34.0f, 88.0f));
	}

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	constexpr float ContactMargin = 15.0f;
	const FBox TouchBox = ActorBox.ExpandBy(ContactMargin);

	// Zakres wokseli obejmowanych przez bryłę aktora w przestrzeni siatki
	const int32 MinX = FMath::FloorToInt(TouchBox.Min.X / SafeCellSize);
	const int32 MaxX = FMath::FloorToInt(TouchBox.Max.X / SafeCellSize);
	const int32 MinY = FMath::FloorToInt(TouchBox.Min.Y / SafeCellSize);
	const int32 MaxY = FMath::FloorToInt(TouchBox.Max.Y / SafeCellSize);
	const int32 MinZ = FMath::FloorToInt(TouchBox.Min.Z / SafeCellSize);
	const int32 MaxZ = FMath::FloorToInt(TouchBox.Max.Z / SafeCellSize);

	constexpr ESurfaceFaceDirection AllFaces[6] = {
		ESurfaceFaceDirection::Up,
		ESurfaceFaceDirection::Down,
		ESurfaceFaceDirection::North,
		ESurfaceFaceDirection::South,
		ESurfaceFaceDirection::East,
		ESurfaceFaceDirection::West
	};

	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 Z = MinZ; Z <= MaxZ; ++Z)
			{
				for (ESurfaceFaceDirection Face : AllFaces)
				{
					const FSurfaceCellCoord Candidate(X, Y, Z, Face);
					if (ActiveCells.Contains(Candidate))
					{
						const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Face);
						const FVector Center = Candidate.ToWorldLocation(SafeCellSize);

						// Wyznaczamy cienką powłokę powierzchniową komórki
						FVector CellHalfExtent(SafeCellSize * 0.5f);
						if (Face == ESurfaceFaceDirection::Up || Face == ESurfaceFaceDirection::Down)
						{
							CellHalfExtent.Z = ContactMargin;
						}
						else if (Face == ESurfaceFaceDirection::North || Face == ESurfaceFaceDirection::South)
						{
							CellHalfExtent.X = ContactMargin;
						}
						else
						{
							CellHalfExtent.Y = ContactMargin;
						}

						const FVector SurfaceCenter = Center - Normal * (SafeCellSize * 0.5f - ContactMargin * 0.5f);
						const FBox SurfaceBox(SurfaceCenter - CellHalfExtent, SurfaceCenter + CellHalfExtent);

						if (ActorBox.Intersect(SurfaceBox))
						{
							OutCoords.AddUnique(Candidate);
						}
					}
				}
			}
		}
	}
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
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
	const FVector Center = Coord.ToWorldLocation(SafeCellSize);
	FHitResult Hit;
	return ProbeSurfaceAt(GetWorld(), Center, Normal, SafeCellSize * 0.8f, Hit, OutMaterial);
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
		// Bezpiecznik: jeśli komórka została usunięta z ActiveCells w trakcie tej samej pętli
		// (np. reakcja/wybuch na wcześniejszej komórce zniszczył podtrzymującą strukturę lochu i usunął komórki), pomijamy ją!
		if (!ActiveCells.Contains(Coord))
		{
			continue;
		}

		ProcessedCoords.Add(Coord);
		if (ApplyStatusToCell(Coord, Status, Duration, Instigator))
		{
			AffectedCount++;
		}
	}

	// 3. Wszechkierunkowa projekcja wybuchu na otaczające powierzchnie lochu (posadzka, sufit, ściany, rampy)
	const TArray<FVector>& ScanDirections = GetBurstScanDirections();

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfaceBurstTrace), false, Instigator);
	if (Instigator)
	{
		TraceParams.AddIgnoredActor(Instigator);
	}

	for (const FVector& RayDir : ScanDirections)
	{
		const float TraceDist = (RayDir.Z < -0.9f) ? (Radius + 100.0f) : Radius;
		const FVector TraceEnd = Origin + RayDir * TraceDist;

		FHitResult SurfaceHit;
		if (GetWorld()->LineTraceSingleByChannel(SurfaceHit, Origin, TraceEnd, ECC_Visibility, TraceParams))
		{
			if (SurfaceHit.GetActor() && IsValidSurfaceTarget(SurfaceHit.GetActor()))
			{
				const float DistToSurface = FMath::Clamp(SurfaceHit.Distance, 0.0f, Radius);
				const float BaseDiscRadius = FMath::Sqrt(FMath::Max(0.0f, RadiusSq - FMath::Square(DistToSurface)));
				const float SplashRadius = (RayDir.Z < -0.9f) ? (Radius * 0.75f) : FMath::Clamp(BaseDiscRadius * 0.75f, SafeCellSize * 0.5f, Radius);

				AffectedCount += PaintSurfaceInternal(
					SurfaceHit.ImpactPoint,
					SurfaceHit.ImpactNormal,
					SplashRadius,
					Status,
					Duration,
					Instigator,
					&ProcessedCoords);
			}
		}
	}

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

	// 2. Propagacja żywiołów na sąsiednie komórki (Cellular Automata)
	PropagateElementalSpreads(CurrentTime, SafeCellSize);

	// 3. Server-Authoritative: dwukierunkowa interakcja żywiołowa między obiektami a komórkami
	if (World->GetNetMode() != NM_Client && ActiveCells.Num() > 0)
	{
		ProcessActorInteractions(CurrentTime);
	}

	// 4. Debug visuals
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
			if (Cell.ActiveStatuses[Index].ServerEndTime > 0.0f && CurrentTime >= Cell.ActiveStatuses[Index].ServerEndTime)
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
			UElementalReactionRules::CleanOrphanedStatuses(Cell);
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
			const FSurfaceCellData* NeighborData = ActiveCells.Find(NeighborCoord);
			if (!NeighborData || NeighborData->IsEmpty())
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

			for (const FSurfaceCellStatusEntry& SourceEntry : SourceData.ActiveStatuses)
			{
				FPendingSpreadCell Spread;
				if (TryEvaluateSpreadReaction(NeighborCoord, *NeighborData, SourceEntry, CurrentTime, Spread))
				{
					if (FPendingSpreadCell* ExistingPending = PendingSpreads.Find(NeighborCoord))
					{
						ExistingPending->Duration = FMath::Max(ExistingPending->Duration, Spread.Duration);
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

void UDungeonSurfaceSubsystem::ProcessActorInteractions(float CurrentTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Jeśli rejestr jest pusty (np. po Live Coding w trakcie sesji), uzupełniamy go
	if (RegisteredStatusComponents.Num() == 0)
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (APawn* Pawn = *It)
			{
				if (UStatusEffectComponent* StatusComp = Pawn->FindComponentByClass<UStatusEffectComponent>())
				{
					RegisteredStatusComponents.AddUnique(StatusComp);
				}
			}
		}
		for (TActorIterator<AInteractivePropBase> It(World); It; ++It)
		{
			if (AInteractivePropBase* Prop = *It)
			{
				if (UStatusEffectComponent* StatusComp = Prop->FindComponentByClass<UStatusEffectComponent>())
				{
					RegisteredStatusComponents.AddUnique(StatusComp);
				}
			}
		}
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
	GetCellsTouchingActor(Actor, TouchedCells);
	if (TouchedCells.Num() == 0)
	{
		return;
	}

	TArray<EStatusEffectType> ActorStatuses = StatusComp->GetActiveStatuses();
	UElementalReactionRules::SortByReactionPriority(ActorStatuses);

	TMap<EStatusEffectType, TWeakObjectPtr<AActor>> FloorStatusesToApply;
	EStatusEffectType DominantLiquid = EStatusEffectType::None;
	TWeakObjectPtr<AActor> DominantLiquidInstigator = nullptr;
	float MinLiquidDistSq = TNumericLimits<float>::Max();
	const FVector ActorLocation = Actor->GetActorLocation();
	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	for (const FSurfaceCellCoord& CellCoord : TouchedCells)
	{
		FSurfaceCellData* CellData = ActiveCells.Find(CellCoord);
		if (!CellData || CellData->IsEmpty())
		{
			continue;
		}

		// A. Interakcja Obiekt -> Komórka (z zachowaniem priorytetu reakcji i braku fałszywego break)
		for (int32 StatusIdx = 0; StatusIdx < ActorStatuses.Num(); ++StatusIdx)
		{
			const EStatusEffectType ActorStatus = ActorStatuses[StatusIdx];
			if (ActorStatus == EStatusEffectType::None || ActorStatus == EStatusEffectType::Oiled)
			{
				continue; // Olej na ciele aktora jest pasywny - nie wylewa się na posadzkę
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
				if (Reaction.ReactionTag == FName(TEXT("Liquid_Displaced")))
				{
					continue;
				}

				// Bezpiecznik fizyczny: jeśli komórka już posiada status docelowy, który ta reakcja wprowadza
				// (np. komórka już płonie [Oiled, Burning] lub jest naelektryzowana [Wet, Electrified]),
				// to nie aplikujemy go ponownie z aktora, aby nie tworzyć pętli sprzężenia zwrotnego.
				const EStatusEffectType TargetStatus = (Reaction.ResultingStatus != EStatusEffectType::None) ? Reaction.ResultingStatus : ActorStatus;
				if (CellData->HasStatus(TargetStatus))
				{
					continue;
				}

				// Zadawanie natychmiastowych obrażeń reakcji (np. wybuch oleju, szok przewodzenia)
				if (Reaction.BonusInstantDamage > 0.0f)
				{
					if (UDamageableComponent* Damageable = Actor->FindComponentByClass<UDamageableComponent>())
					{
						Damageable->ApplyDamage(Reaction.BonusInstantDamage);
						if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
						{
							return;
						}
					}
				}

				// Jeśli status obiektu uległ zużyciu w reakcji (np. woda na obiekcie gasi ogień na posadzce, lub woda na posadzce gasi ogień na obiekcie)
				if (Reaction.bConsumeIncomingStatus)
				{
					StatusComp->RemoveStatus(ActorStatus);
					ActorStatuses.RemoveAt(StatusIdx);
					--StatusIdx;
				}

				const float ReactionDuration = (Reaction.ResultingDuration > 0.0f ? Reaction.ResultingDuration : 5.0f);
				ApplyStatusToCell(CellCoord, TargetStatus, ReactionDuration, Actor);

				// Po ewentualnej modyfikacji komórki przez ApplyStatusToCell odświeżamy wskaźnik
				CellData = ActiveCells.Find(CellCoord);
			}
		}

		// Zbieranie statusów z tej komórki do unikalnego zbioru dla obiektu
		if (CellData && !CellData->IsEmpty())
		{
			const float CellDistSq = FVector::DistSquared(CellCoord.ToWorldLocation(SafeCellSize), ActorLocation);

			for (const FSurfaceCellStatusEntry& Entry : CellData->ActiveStatuses)
			{
				// ZŁOTA ZASADA CHEMICZNA: Na styku komórek postać może w danym ticku przyjąć tylko JEDEN płyn (z najbliższej komórki)
				if (UElementalReactionRules::IsLiquidStatus(Entry.Status))
				{
					if (CellDistSq < MinLiquidDistSq)
					{
						MinLiquidDistSq = CellDistSq;
						DominantLiquid = Entry.Status;
						DominantLiquidInstigator = Entry.Instigator;
					}
				}
				else if (!FloorStatusesToApply.Contains(Entry.Status))
				{
					FloorStatusesToApply.Add(Entry.Status, Entry.Instigator);
				}
			}
		}
	}

	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
	{
		return;
	}

	// B. Interakcja Komórka -> Obiekt:
	// 1. ZŁOTA ZASADA: NAJPIERW aplikujemy płyn podłoża (fizyczny nośnik otoczenia, np. woda zmywa olej)
	if (DominantLiquid != EStatusEffectType::None)
	{
		UE_LOG(LogDungeonElements, Verbose, TEXT("[ProcessGridTick] Applying dominant floor liquid %s to %s"),
			*UEnum::GetValueAsString(DominantLiquid), *Actor->GetName());
		StatusComp->ApplyStatus(DominantLiquid, 0, -1.0f, DominantLiquidInstigator.Get());

		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			return;
		}
	}

	// 2. NASTĘPNIE aplikujemy pozostałe statusy środowiskowe (energia: prąd, ogień)
	for (const auto& StatusPair : FloorStatusesToApply)
	{
		if (StatusPair.Key != DominantLiquid)
		{
			UE_LOG(LogDungeonElements, Verbose, TEXT("[ProcessGridTick] Applying secondary floor status %s to %s"),
				*UEnum::GetValueAsString(StatusPair.Key), *Actor->GetName());
			StatusComp->ApplyStatus(StatusPair.Key, 0, -1.0f, StatusPair.Value.Get());
		}
	}
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

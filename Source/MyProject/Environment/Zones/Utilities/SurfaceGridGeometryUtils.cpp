#include "SurfaceGridGeometryUtils.h"

#include "Engine/World.h"
#include "Engine/Brush.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Dungeon/Props/InteractivePropBase.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "MyProject/Shared/Interfaces/SurfaceGridTargetInterface.h"

namespace SurfaceGridGeometryUtils
{
	bool IsValidSurfaceTarget(const AActor* Actor)
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

		// 2. Akceptujemy aktorów implementujących ISurfaceGridTargetInterface (fundamenty, mechanizmy, bramy, pułapki)
		if (Actor->GetClass()->ImplementsInterface(USurfaceGridTargetInterface::StaticClass()))
		{
			return ISurfaceGridTargetInterface::Execute_CanReceiveSurfaceCells(Actor);
		}

		// 3. Fallback dla architektury lochu (ADungeonStructureBase)
		if (const ADungeonStructureBase* Structure = Cast<ADungeonStructureBase>(Actor))
		{
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

		// 4. Akceptujemy geometrię poziomu (BSP Brushes map testowych i prototypowych)
		if (Actor->IsA<ABrush>())
		{
			return true;
		}

		return false;
	}

	bool IsDynamicSurfaceTarget(const AActor* Actor)
	{
		return Actor && Actor->Implements<USurfaceGridTargetInterface>()
			&& ISurfaceGridTargetInterface::Execute_IsDynamicSurface(Actor);
	}

	bool GetDebugViewerLocation(const UWorld* World, FVector& OutViewerLocation)
	{
		if (!World)
		{
			return false;
		}

		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				OutViewerLocation = PC->PlayerCameraManager->GetCameraLocation();
				return true;
			}
			if (const APawn* Pawn = PC->GetPawn())
			{
				OutViewerLocation = Pawn->GetActorLocation();
				return true;
			}
		}
		return false;
	}

	EPhysicalMaterialType GetMaterialFromActor(const AActor* Actor)
	{
		if (Actor && Actor->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
		{
			return IMaterialProviderInterface::Execute_GetMaterialType(Actor);
		}
		return EPhysicalMaterialType::Stone;
	}

	bool HasSufficientSurfaceCoverage(
		const AActor* Actor,
		const FSurfaceCellCoord& Coord,
		float CellSize,
		float MinAxisCoverageFraction)
	{
		if (!Actor || !IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			return false;
		}

		FBox ActorBox = Actor->GetComponentsBoundingBox(false);
		if (!ActorBox.IsValid)
		{
			ActorBox = Actor->GetComponentsBoundingBox(true);
		}

		if (!ActorBox.IsValid)
		{
			return true;
		}

		const float SafeCellSize = FMath::Max(1.0f, CellSize);
		const float SafeMinCoverage = FMath::Clamp(MinAxisCoverageFraction, 0.01f, 0.99f);

		// Granice woksela komórki w przestrzeni świata
		const float CellMinX = static_cast<float>(Coord.X) * SafeCellSize;
		const float CellMaxX = static_cast<float>(Coord.X + 1) * SafeCellSize;
		const float CellMinY = static_cast<float>(Coord.Y) * SafeCellSize;
		const float CellMaxY = static_cast<float>(Coord.Y + 1) * SafeCellSize;
		const float CellMinZ = static_cast<float>(Coord.Z) * SafeCellSize;
		const float CellMaxZ = static_cast<float>(Coord.Z + 1) * SafeCellSize;

		float OverlapU = 0.0f;
		float OverlapV = 0.0f;

		switch (Coord.Face)
		{
		case ESurfaceFaceDirection::Up:
		case ESurfaceFaceDirection::Down:
			// Płaszczyzna XY (Podłoga / Sufit)
			OverlapU = FMath::Max(0.0f, FMath::Min(CellMaxX, ActorBox.Max.X) - FMath::Max(CellMinX, ActorBox.Min.X));
			OverlapV = FMath::Max(0.0f, FMath::Min(CellMaxY, ActorBox.Max.Y) - FMath::Max(CellMinY, ActorBox.Min.Y));
			break;

		case ESurfaceFaceDirection::North:
		case ESurfaceFaceDirection::South:
			// Płaszczyzna YZ (Ściany North / South)
			OverlapU = FMath::Max(0.0f, FMath::Min(CellMaxY, ActorBox.Max.Y) - FMath::Max(CellMinY, ActorBox.Min.Y));
			OverlapV = FMath::Max(0.0f, FMath::Min(CellMaxZ, ActorBox.Max.Z) - FMath::Max(CellMinZ, ActorBox.Min.Z));
			break;

		case ESurfaceFaceDirection::East:
		case ESurfaceFaceDirection::West:
			// Płaszczyzna XZ (Ściany East / West)
			OverlapU = FMath::Max(0.0f, FMath::Min(CellMaxX, ActorBox.Max.X) - FMath::Max(CellMinX, ActorBox.Min.X));
			OverlapV = FMath::Max(0.0f, FMath::Min(CellMaxZ, ActorBox.Max.Z) - FMath::Max(CellMinZ, ActorBox.Min.Z));
			break;
		}

		const float FractionU = OverlapU / SafeCellSize;
		const float FractionV = OverlapV / SafeCellSize;

		// Wymagamy, aby aktor pokrywał co najmniej SafeMinCoverage komórki wzdłuż każdej z dwóch osi stycznych
		if (FractionU < SafeMinCoverage || FractionV < SafeMinCoverage)
		{
			return false;
		}

		return true;
	}

	bool ProbeSurfaceAt(
		const UWorld* World,
		const FVector& ProbeLocation,
		const FVector& SurfaceNormal,
		float ProbeDistance,
		FHitResult& OutHit,
		EPhysicalMaterialType& OutMaterial,
		const FCollisionQueryParams& Params)
	{
		OutMaterial = EPhysicalMaterialType::Stone;
		if (!World)
		{
			return false;
		}

		const FVector ProbeStart = ProbeLocation + SurfaceNormal * 20.0f;
		const FVector ProbeEnd = ProbeLocation - SurfaceNormal * ProbeDistance;

		TArray<FHitResult> Hits;
		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
		if (World->LineTraceMultiByObjectType(Hits, ProbeStart, ProbeEnd, ObjectParams, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				AActor* HitActor = Hit.GetActor();
				if (HitActor && IsValidSurfaceTarget(HitActor))
				{
					if (FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) > 0.4f)
					{
						OutHit = Hit;
						if (FVector::DotProduct(OutHit.ImpactNormal, SurfaceNormal) < 0.0f)
						{
							OutHit.ImpactNormal = -OutHit.ImpactNormal;
						}
						OutMaterial = GetMaterialFromActor(HitActor);
						return true;
					}
				}
			}
		}
		return false;
	}

	bool CheckSurfacePresenceAt(
		const UWorld* World,
		const FVector& SamplePoint,
		const FVector& SurfaceNormal,
		FHitResult& OutHit,
		const FCollisionQueryParams& Params)
	{
		EPhysicalMaterialType IgnoredMat;
		return ProbeSurfaceAt(World, SamplePoint, SurfaceNormal, 30.0f, OutHit, IgnoredMat, Params);
	}

	bool HasSurfaceLineOfSight(
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
		const float TotalDist = FVector::Dist(LoSStart, LoSEnd);

		TArray<FHitResult> Hits;
		if (World->LineTraceMultiByChannel(Hits, LoSStart, LoSEnd, ECC_Visibility, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				if (!Hit.bBlockingHit)
				{
					continue;
				}

				// Wykrywamy przeszkody poprzeczne/pionowe do powierzchni (ściany, kolumny, skrzynie, rekwizyty)
				const bool bIsObstacle = FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) < 0.6f;
				if (bIsObstacle && Hit.Distance < TotalDist - 2.0f)
				{
					return false;
				}
			}
		}

		return true;
	}

	bool HasDirectBurstLineOfSight(
		const UWorld* World,
		const FVector& BurstOrigin,
		const FVector& TargetSurfacePoint,
		const FVector& SurfaceNormal,
		const AActor* TargetSurfaceActor,
		const FCollisionQueryParams& Params)
	{
		if (!World)
		{
			return false;
		}

		const FVector ToOrigin = BurstOrigin - TargetSurfacePoint;
		const float DistSq = ToOrigin.SizeSquared();
		if (DistSq <= 1.0f)
		{
			return true;
		}

		const float Dist = FMath::Sqrt(DistSq);
		const FVector DirToOrigin = ToOrigin / Dist;

		// 1. Sprawdzenie orientacji normalnej (Backface Culling z tolerancją na płaszczyznę wybuchu):
		// Prawdziwa przeciwna strona ściany/podłogi/sufitu ma DotProduct bliski -1.0 (kąt > 107°).
		// Na samej płaszczyźnie wybuchu (zwłaszcza sufitu) wektor do źródła leży w płaszczyźnie (Dot wokół 0),
		// więc próg -0.3f chroni przed fałszywym odrzuceniem komórek na tej samej powierzchni.
		if (FVector::DotProduct(SurfaceNormal, DirToOrigin) < -0.3f)
		{
			return false;
		}

		// 2. Promień testowy LoS w przestrzeni 3D:
		// Cel odsuwamy o 8 cm wzdłuż normalnej w wolną przestrzeń lochu,
		// aby promień nie szorował po mikronierównościach (np. szczeliny, kamienie w posadzce).
		const FVector LoSTarget = TargetSurfacePoint + SurfaceNormal * 8.0f;

		// Start odsuwamy minimalnie w kierunku celu, by nie uderzyć w ewentualną ścianę/podłoże tuż za BurstOrigin
		const FVector LoSStart = BurstOrigin - DirToOrigin * 2.0f;
		const float TotalTraceDist = FVector::Dist(LoSStart, LoSTarget);

		// Używamy LineTraceMultiByChannel, aby nie zatrzymać się przedwcześnie na płytkim muśnięciu
		// tej samej nierównej posadzki przed faktyczną przeszkodą (ścianą, filarem, skrzynią).
		TArray<FHitResult> Hits;
		if (World->LineTraceMultiByChannel(Hits, LoSStart, LoSTarget, ECC_Visibility, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				if (!Hit.bBlockingHit)
				{
					continue;
				}

				// Jeśli trafienie nastąpiło tuż przy samym celu (ostatnie 15 cm trasy przed komórką)
				if (Hit.Distance >= TotalTraceDist - 15.0f)
				{
					continue;
				}

				// Sprawdzamy czy to nie jest płaskie muśnięcie tej samej, współpłaszczyznowej podłogi/ściany/sufitu
				// (np. sąsiednie moduły posadzki 300x300 cm lub drobny występ na tej samej powierzchni)
				const bool bIsParallelNormal = FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) > 0.7f;
				const float PlaneDist = FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - TargetSurfacePoint, SurfaceNormal));
				if (bIsParallelNormal && PlaneDist < 8.0f)
				{
					// Płaskie muśnięcie współpłaszczyznowego podłoża po drodze nie blokuje wybuchu
					continue;
				}

				// Promień natrafił na rzeczywistą przeszkodę poprzeczną (ścianę, filar, barykadę, skrzynię, rekwizyt)
				return false;
			}
		}

		return true;
	}

	void GetFaceTangents(ESurfaceFaceDirection Face, FVector& OutTangentU, FVector& OutTangentV)
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

	const TArray<FVector>& GetBurstScanDirections()
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

	void FindSpreadCandidates(
		const UWorld* World,
		const FSurfaceCellCoord& SourceCoord,
		const AActor* SourceActor,
		const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
		float CellSize,
		TArray<FSurfaceSpreadCandidate>& OutCandidates)
	{
		OutCandidates.Reset();
		if (!World)
		{
			return;
		}

		TArray<FSurfaceSpreadPath, TInlineAllocator<4>> SpreadPaths;
		SourceCoord.GetDirectionalSpreadPaths(SpreadPaths);

		auto QuerySurface = [World, &ActiveCells, CellSize](
			const FSurfaceCellCoord& Coord,
			EPhysicalMaterialType& OutMat,
			AActor*& OutActor,
			TArray<EStatusEffectType>& OutStatuses,
			FSurfaceCellCoord* OutResolvedCoord = nullptr) -> bool
		{
			OutStatuses.Reset();
			if (const FSurfaceCellData* Existing = ActiveCells.Find(Coord))
			{
				if (!Existing->IsEmpty())
				{
					if (OutResolvedCoord)
					{
						*OutResolvedCoord = Coord;
					}
					OutMat = Existing->SurfaceMaterial;
					OutActor = Existing->SurfaceActor.Get();
					OutStatuses = Existing->GetStatusTypes();
					return true;
				}
			}

			const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
			const FVector Center = Coord.ToWorldLocation(CellSize);
			FHitResult Hit;
			const bool bHit = ProbeSurfaceAt(World, Center, Normal, CellSize * 0.8f, Hit, OutMat);
			if (bHit)
			{
				OutActor = Hit.GetActor();
				const FSurfaceCellCoord HitCoord = FSurfaceCellCoord::FromWorldLocation(Hit.ImpactPoint, Normal, CellSize);
				if (OutResolvedCoord)
				{
					*OutResolvedCoord = HitCoord;
				}

				if (const FSurfaceCellData* ExistingHit = ActiveCells.Find(HitCoord))
				{
					if (!ExistingHit->IsEmpty())
					{
						OutMat = ExistingHit->SurfaceMaterial;
						OutActor = ExistingHit->SurfaceActor.Get();
						OutStatuses = ExistingHit->GetStatusTypes();
						return true;
					}
				}

				if (OutActor && !HasSufficientSurfaceCoverage(OutActor, HitCoord, CellSize))
				{
					return false;
				}
			}
			return bHit;
		};

		for (const auto& Path : SpreadPaths)
		{
			// 1. Sprawdzamy kandydata współpłaszczyznowego (Coplanar)
			EPhysicalMaterialType CoplanarMat = EPhysicalMaterialType::Stone;
			AActor* CoplanarActor = nullptr;
			TArray<EStatusEffectType> CoplanarStatuses;

			const bool bCoplanarHit = QuerySurface(Path.Coplanar, CoplanarMat, CoplanarActor, CoplanarStatuses);
			if (bCoplanarHit)
			{
				OutCandidates.Add({ Path.Coplanar, CoplanarMat, CoplanarActor, MoveTemp(CoplanarStatuses), false });
			}

			// Pobieramy wszystkich potencjalnych kandydatów wokseli na krawędzi narożnika 90°
			TArray<FSurfaceCellCoord, TInlineAllocator<4>> CornerVariants;
			SourceCoord.GetCornerCandidateCoords(Path.CornerFace, CornerVariants);
			if (CornerVariants.IsEmpty())
			{
				CornerVariants.Add(FSurfaceCellCoord(SourceCoord.X, SourceCoord.Y, SourceCoord.Z, Path.CornerFace));
			}

			// Jeśli płaszczyzna kontynuuje się w linii prostej na wprost (bCoplanarHit == true),
			// ściana pionowa NIE zagina się w tym miejscu pod kątem 90° w kolejną ścianę pionową,
			// CHYBA ŻE w siatce ActiveCells istnieje już aktywna komórka na którymkolwiek wariancie narożnika.
			// Nie dotyczy to poziomych krawędzi stropu (Down) i posadzki (Up), gdzie narożnik 90° istnieje naturalnie.
			const bool bIsWallToWall = (SourceCoord.Face != ESurfaceFaceDirection::Up && SourceCoord.Face != ESurfaceFaceDirection::Down)
									&& (Path.CornerFace != ESurfaceFaceDirection::Up && Path.CornerFace != ESurfaceFaceDirection::Down);

			if (bCoplanarHit && bIsWallToWall)
			{
				bool bHasActiveCornerCell = false;
				for (const FSurfaceCellCoord& Variant : CornerVariants)
				{
					if (const FSurfaceCellData* ExistingCorner = ActiveCells.Find(Variant))
					{
						if (!ExistingCorner->IsEmpty())
						{
							bHasActiveCornerCell = true;
							break;
						}
					}
				}
				if (!bHasActiveCornerCell)
				{
					continue;
				}
			}

			// 2. Rozwiązujemy narożnik wklęsły 90° (Corner) spośród dostępnych wariantów CornerVariants:
			// Priorytet A: Szukamy komórki, która już istnieje i jest aktywna w ActiveCells (np. wylany olej, woda na suficie/ścianie).
			// Wybieramy tę leżącą najbliżej środka komórki źródłowej.
			const FSurfaceCellCoord* BestActiveCoord = nullptr;
			const FSurfaceCellData* BestActiveData = nullptr;
			float BestDistSq = MAX_flt;
			const FVector SourceCenter = SourceCoord.ToWorldLocation(CellSize);

			for (const FSurfaceCellCoord& Variant : CornerVariants)
			{
				if (const FSurfaceCellData* Existing = ActiveCells.Find(Variant))
				{
					if (!Existing->IsEmpty())
					{
						const float DistSq = FVector::DistSquared(SourceCenter, Variant.ToWorldLocation(CellSize));
						if (DistSq < BestDistSq)
						{
							BestDistSq = DistSq;
							BestActiveCoord = &Variant;
							BestActiveData = Existing;
						}
					}
				}
			}

			if (BestActiveCoord && BestActiveData)
			{
				AActor* CornerActor = BestActiveData->SurfaceActor.Get();
				// Ponieważ komórka jest już aktywna w siatce, gracz lub system celowo nałożył tam ciecz/efekt,
				// więc nie blokujemy własnej struktury.
				OutCandidates.Add({ *BestActiveCoord, BestActiveData->SurfaceMaterial, CornerActor, BestActiveData->GetStatusTypes(), true });
				continue;
			}

			// Priorytet B: Jeśli brak aktywnej komórki w siatce, sondujemy geometrię świata (QuerySurface)
			// dla wariantów narożnika, aby odnaleźć surową architekturę (np. drewniany sufit / drewnianą podłogę).
			bool bFoundCornerProbe = false;
			FSurfaceCellCoord ResolvedProbeCoord;
			EPhysicalMaterialType ResolvedMat = EPhysicalMaterialType::Stone;
			AActor* ResolvedActor = nullptr;
			TArray<EStatusEffectType> ResolvedStatuses;
			float BestProbeDistSq = MAX_flt;

			for (const FSurfaceCellCoord& Variant : CornerVariants)
			{
				FSurfaceCellCoord HitCoord;
				EPhysicalMaterialType TestMat = EPhysicalMaterialType::Stone;
				AActor* TestActor = nullptr;
				TArray<EStatusEffectType> TestStatuses;

				if (QuerySurface(Variant, TestMat, TestActor, TestStatuses, &HitCoord))
				{
					const float DistSq = FVector::DistSquared(SourceCenter, HitCoord.ToWorldLocation(CellSize));
					if (DistSq < BestProbeDistSq)
					{
						BestProbeDistSq = DistSq;
						ResolvedProbeCoord = HitCoord;
						ResolvedMat = TestMat;
						ResolvedActor = TestActor;
						ResolvedStatuses = MoveTemp(TestStatuses);
						bFoundCornerProbe = true;
					}
				}
			}

			if (bFoundCornerProbe)
			{
				// ZASADA SEPARACJI STRUKTUR: Obiekt nie może podpalać/razić prostopadłych krawędzi samego siebie,
				// CHYBA ŻE w siatce istnieje już aktywna komórka w tym miejscu
				const bool bAlreadyActive = !ResolvedStatuses.IsEmpty();
				if (!bAlreadyActive && ResolvedActor && ResolvedActor == SourceActor)
				{
					continue;
				}

				OutCandidates.Add({ ResolvedProbeCoord, ResolvedMat, ResolvedActor, MoveTemp(ResolvedStatuses), true });
			}
		}
	}
}

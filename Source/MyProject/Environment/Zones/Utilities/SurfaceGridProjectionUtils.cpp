#include "SurfaceGridProjectionUtils.h"
#include "SurfaceGridGeometryUtils.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"

void SurfaceGridProjectionUtils::ProjectStatusToSurface(
	const UWorld* World,
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	float CellSize,
	const AActor* Instigator,
	FSurfaceCellCandidateCallback OnCellCandidate)
{
	if (!World || Radius <= 0.0f)
	{
		return;
	}

	const ESurfaceFaceDirection FaceDir = SurfaceGridUtils::NormalToFaceDirection(HitNormal);
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(FaceDir);

	// Wyznaczamy wektory styczne do płaszczyzny ściany/podłogi
	FVector TangentU;
	FVector TangentV;
	SurfaceGridGeometryUtils::GetFaceTangents(FaceDir, TangentU, TangentV);

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const int32 StepRadius = (Radius <= SafeCellSize * 0.5f) ? 0 : FMath::CeilToInt(Radius / SafeCellSize);
	const float RadiusSq = FMath::Square(Radius);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfaceStatusValidation), false, Instigator);
	if (Instigator)
	{
		TraceParams.AddIgnoredActor(Instigator);
	}

	TSet<FSurfaceCellCoord> ProcessedCoords;
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

			// Szybki pre-check współrzędnej w pamięci: pomijamy zduplikowane próbki bez odpalania raycastów
			const FSurfaceCellCoord FastCoord = FSurfaceCellCoord::FromWorldLocation(SamplePoint, Normal, SafeCellSize);
			if (ProcessedCoords.Contains(FastCoord))
			{
				continue;
			}

			// 1. Drop-Off Test: Sprawdzamy, czy pod próbką fizycznie istnieje architektura
			FHitResult SurfaceHit;
			if (!SurfaceGridGeometryUtils::CheckSurfacePresenceAt(World, SamplePoint, Normal, SurfaceHit, TraceParams))
			{
				continue;
			}

			// 2. Line of Sight po powierzchni od punktu uderzenia (PointImpact / 2D)
			if (!Offset.IsNearlyZero())
			{
				if (!SurfaceGridGeometryUtils::HasSurfaceLineOfSight(World, HitLocation, SamplePoint, Normal, TraceParams))
				{
					continue;
				}
			}

			const FSurfaceCellCoord Coord = FSurfaceCellCoord::FromWorldLocation(SurfaceHit.ImpactPoint, Normal, SafeCellSize);

			// Weryfikacja minimalnego pokrycia (Min Coverage Threshold):
			// Odrzucamy komórki wiszące w większości poza obiektem (zasada większości min. 50%).
			if (!SurfaceGridGeometryUtils::HasSufficientSurfaceCoverage(SurfaceHit.GetActor(), Coord, SafeCellSize))
			{
				continue;
			}

			if (ProcessedCoords.Contains(Coord))
			{
				continue;
			}
			ProcessedCoords.Add(Coord);
			ProcessedCoords.Add(FastCoord);

			const EPhysicalMaterialType HitMat = SurfaceGridGeometryUtils::GetMaterialFromActor(SurfaceHit.GetActor());

			OnCellCandidate(Coord, HitMat, SurfaceHit.GetActor(), SurfaceHit.ImpactPoint);
		}
	}
}

void SurfaceGridProjectionUtils::ProjectStatusInArea(
	const UWorld* World,
	const FVector& HitLocation,
	const FVector& HitNormal,
	float Radius,
	const FVector& BurstOrigin,
	float CellSize,
	TSet<FSurfaceCellCoord>& InOutProcessedCoords,
	const AActor* Instigator,
	FSurfaceCellCandidateCallback OnCellCandidate)
{
	if (!World || Radius <= 0.0f)
	{
		return;
	}

	const ESurfaceFaceDirection FaceDir = SurfaceGridUtils::NormalToFaceDirection(HitNormal);
	const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(FaceDir);

	// Wyznaczamy wektory styczne do płaszczyzny ściany/podłogi
	FVector TangentU;
	FVector TangentV;
	SurfaceGridGeometryUtils::GetFaceTangents(FaceDir, TangentU, TangentV);

	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const int32 StepRadius = (Radius <= SafeCellSize * 0.5f) ? 0 : FMath::CeilToInt(Radius / SafeCellSize);
	const float RadiusSq = FMath::Square(Radius);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfaceStatusValidation), false, Instigator);
	if (Instigator)
	{
		TraceParams.AddIgnoredActor(Instigator);
	}

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

			// Szybki pre-check współrzędnej w pamięci: jeśli komórka została już zbadana w Step 1 (z ActiveCells)
			// lub przez nachodzący na siebie promień wybuchu, pomijamy ją BEZ ODPALANIA JAKICHKOLWIEK RAYCASTÓW!
			const FSurfaceCellCoord FastCoord = FSurfaceCellCoord::FromWorldLocation(SamplePoint, Normal, SafeCellSize);
			if (InOutProcessedCoords.Contains(FastCoord))
			{
				continue;
			}

			// 1. Drop-Off Test: Sprawdzamy, czy pod próbką fizycznie istnieje architektura
			FHitResult SurfaceHit;
			if (!SurfaceGridGeometryUtils::CheckSurfacePresenceAt(World, SamplePoint, Normal, SurfaceHit, TraceParams))
			{
				continue;
			}

			// 2. Line of Sight z BurstOrigin do punktu na powierzchni (RadialBurst / 3D)
			if (!SurfaceGridGeometryUtils::HasDirectBurstLineOfSight(World, BurstOrigin, SurfaceHit.ImpactPoint, Normal, SurfaceHit.GetActor(), TraceParams))
			{
				continue;
			}

			const FSurfaceCellCoord Coord = FSurfaceCellCoord::FromWorldLocation(SurfaceHit.ImpactPoint, Normal, SafeCellSize);

			// Weryfikacja minimalnego pokrycia (Min Coverage Threshold):
			// Odrzucamy komórki wiszące w większości poza obiektem (zasada większości min. 50%).
			if (!SurfaceGridGeometryUtils::HasSufficientSurfaceCoverage(SurfaceHit.GetActor(), Coord, SafeCellSize))
			{
				continue;
			}

			// Pomijamy koordynaty już przetworzone w ramach tego samego zdarzenia
			if (InOutProcessedCoords.Contains(Coord))
			{
				continue;
			}
			InOutProcessedCoords.Add(Coord);
			InOutProcessedCoords.Add(FastCoord);

			const EPhysicalMaterialType HitMat = SurfaceGridGeometryUtils::GetMaterialFromActor(SurfaceHit.GetActor());

			OnCellCandidate(Coord, HitMat, SurfaceHit.GetActor(), SurfaceHit.ImpactPoint);
		}
	}
}

void SurfaceGridProjectionUtils::FilterCellsInBurstRadius(
	const UWorld* World,
	const FVector& BurstOrigin,
	float Radius,
	float CellSize,
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
	const AActor* Instigator,
	TArray<FSurfaceCellCoord>& OutCellsInRadius)
{
	OutCellsInRadius.Reset();

	if (!World || Radius <= 0.0f || ActiveCells.Num() == 0)
	{
		return;
	}

	const float RadiusSq = FMath::Square(Radius);
	const float SafeCellSize = FMath::Max(10.0f, CellSize);

	FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(BurstCellLoS), false, Instigator);
	if (Instigator)
	{
		LoSParams.AddIgnoredActor(Instigator);
	}

	for (const auto& Pair : ActiveCells)
	{
		const FSurfaceCellCoord& Coord = Pair.Key;
		const FVector CellWorldPos = Coord.ToWorldLocation(SafeCellSize);
		if (FVector::DistSquared(BurstOrigin, CellWorldPos) <= RadiusSq)
		{
			const FVector CellNormal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
			const FVector CellSurfacePos = CellWorldPos + CellNormal * (SafeCellSize * 0.45f);

			// Ścisły LoS 3D: czy fala wybuchu widzi tę komórkę bez przeszkód
			if (!SurfaceGridGeometryUtils::HasDirectBurstLineOfSight(World, BurstOrigin, CellSurfacePos, CellNormal, Pair.Value.SurfaceActor.Get(), LoSParams))
			{
				continue;
			}

			OutCellsInRadius.Add(Coord);
		}
	}
}

void SurfaceGridProjectionUtils::ScanBurstSurfaces(
	const UWorld* World,
	const FVector& Origin,
	float Radius,
	float CellSize,
	const AActor* Instigator,
	FBurstSurfaceHitCallback OnSurfaceHit)
{
	if (!World || Radius <= 0.0f)
	{
		return;
	}

	const float RadiusSq = FMath::Square(Radius);
	const float SafeCellSize = FMath::Max(10.0f, CellSize);
	const TArray<FVector>& ScanDirections = SurfaceGridGeometryUtils::GetBurstScanDirections();

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SurfaceBurstTrace), false, Instigator);
	if (Instigator)
	{
		TraceParams.AddIgnoredActor(Instigator);
	}

	for (const FVector& RayDir : ScanDirections)
	{
		const float TraceDist = (FMath::Abs(RayDir.Z) > 0.9f) ? (Radius + 100.0f) : Radius;
		const FVector TraceEnd = Origin + RayDir * TraceDist;

		FHitResult SurfaceHit;
		if (World->LineTraceSingleByChannel(SurfaceHit, Origin, TraceEnd, ECC_Visibility, TraceParams))
		{
			if (SurfaceHit.GetActor() && SurfaceGridGeometryUtils::IsValidSurfaceTarget(SurfaceHit.GetActor()))
			{
				// Zapewniamy, że ImpactNormal zawsze przeciwstawia się kierunkowi promienia (ochrona przed odwróconymi normalnymi mesha/Chaos)
				if (FVector::DotProduct(SurfaceHit.ImpactNormal, RayDir) > 0.0f)
				{
					SurfaceHit.ImpactNormal = -SurfaceHit.ImpactNormal;
				}

				const float DistToSurface = FMath::Clamp(SurfaceHit.Distance, 0.0f, Radius);
				const float BaseDiscRadius = FMath::Sqrt(FMath::Max(0.0f, RadiusSq - FMath::Square(DistToSurface)));
				const float SplashRadius = (FMath::Abs(RayDir.Z) > 0.9f) ? (Radius * 0.75f) : FMath::Clamp(BaseDiscRadius * 0.75f, SafeCellSize * 0.5f, Radius);

				OnSurfaceHit(SurfaceHit, SplashRadius);
			}
		}
	}
}

void SurfaceGridProjectionUtils::GetCellsTouchingActor(
	const AActor* Actor,
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
	float CellSize,
	TArray<FSurfaceCellCoord>& OutCoords)
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

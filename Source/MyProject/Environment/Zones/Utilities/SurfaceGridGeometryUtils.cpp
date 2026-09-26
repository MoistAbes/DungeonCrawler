#include "SurfaceGridGeometryUtils.h"

#include "Engine/World.h"
#include "Engine/Brush.h"
#include "GameFramework/Pawn.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

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

	EPhysicalMaterialType GetMaterialFromActor(const AActor* Actor)
	{
		if (Actor && Actor->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
		{
			return IMaterialProviderInterface::Execute_GetMaterialType(Actor);
		}
		return EPhysicalMaterialType::Stone;
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
		const FCollisionObjectQueryParams ObjectParams(ECC_WorldStatic);
		if (World->LineTraceMultiByObjectType(Hits, ProbeStart, ProbeEnd, ObjectParams, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				AActor* HitActor = Hit.GetActor();
				if (HitActor && IsValidSurfaceTarget(HitActor))
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

		auto QuerySurface = [World, &ActiveCells, CellSize](const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMat, AActor*& OutActor) -> bool
		{
			const FSurfaceCellData* Existing = ActiveCells.Find(Coord);
			if (Existing)
			{
				OutMat = Existing->SurfaceMaterial;
				OutActor = Existing->SurfaceActor.Get();
				return true;
			}

			const FVector Normal = SurfaceGridUtils::FaceDirectionToNormal(Coord.Face);
			const FVector Center = Coord.ToWorldLocation(CellSize);
			FHitResult Hit;
			const bool bHit = ProbeSurfaceAt(World, Center, Normal, CellSize * 0.8f, Hit, OutMat);
			if (bHit)
			{
				OutActor = Hit.GetActor();
			}
			return bHit;
		};

		for (const auto& Path : SpreadPaths)
		{
			// 1. Sprawdzamy kandydata współpłaszczyznowego (Coplanar)
			EPhysicalMaterialType CoplanarMat = EPhysicalMaterialType::Stone;
			AActor* CoplanarActor = nullptr;

			if (QuerySurface(Path.Coplanar, CoplanarMat, CoplanarActor))
			{
				OutCandidates.Add({ Path.Coplanar, CoplanarMat, CoplanarActor, false });
				// ZŁOTA ZASADA: Skoro płaszczyzna kontynuuje się w linii prostej, ściana nie zagina się w tym miejscu pod kątem 90°
				continue;
			}

			// 2. Skoro płaszczyzna się skończyła, sprawdzamy narożnik wklęsły 90° (Corner)
			EPhysicalMaterialType CornerMat = EPhysicalMaterialType::Stone;
			AActor* CornerActor = nullptr;

			if (QuerySurface(Path.Corner, CornerMat, CornerActor))
			{
				// ZASADA SEPARACJI STRUKTUR: Obiekt nie może podpalać prostopadłych krawędzi samego siebie
				if (CornerActor && CornerActor == SourceActor)
				{
					continue;
				}

				OutCandidates.Add({ Path.Corner, CornerMat, CornerActor, true });
			}
		}
	}
}

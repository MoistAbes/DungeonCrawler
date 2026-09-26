#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"

class UWorld;
class AActor;

/**
 * Zbiór geometrycznych i fizycznych narzędzi próbkowania powierzchni dla siatki lochu (Surface Grid).
 * Odpowiada za:
 * - Badanie geometrii podłoża i krawędzi (LineTrace / Drop-off test).
 * - Weryfikację widoczności powierzchni (Line of Sight między kafelkami).
 * - Wyznaczanie wektorów stycznych (Tangents) oraz kierunków skanowania wybuchu 3D.
 * - Kwalifikację celów i tożsamości materiałowej architektury.
 */
namespace SurfaceGridGeometryUtils
{
	/** Kwalifikuje, czy dany aktor może być stabilnym podłożem pod komórki powierzchniowe */
	MYPROJECT_API bool IsValidSurfaceTarget(const AActor* Actor);

	/** Rozpoznaje tożsamość materiałową aktora lochu z bezpiecznym fallbackiem do Stone */
	MYPROJECT_API EPhysicalMaterialType GetMaterialFromActor(const AActor* Actor);

	/** Uniwersalny próbnik powierzchni: weryfikuje geometrię architektury i zwraca materiał */
	MYPROJECT_API bool ProbeSurfaceAt(
		const UWorld* World,
		const FVector& ProbeLocation,
		const FVector& SurfaceNormal,
		float ProbeDistance,
		FHitResult& OutHit,
		EPhysicalMaterialType& OutMaterial,
		const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

	/** Sprawdza, czy w danym punkcie fizycznie istnieje płaszczyzna architektury (Drop-off test na krawędziach filarów i ścian) */
	MYPROJECT_API bool CheckSurfacePresenceAt(
		const UWorld* World,
		const FVector& SamplePoint,
		const FVector& SurfaceNormal,
		FHitResult& OutHit,
		const FCollisionQueryParams& Params);

	/** Sprawdza, czy między punktem uderzenia a próbką na powierzchni nie ma przeszkody pionowej (LoS test - filary, narożniki) */
	MYPROJECT_API bool HasSurfaceLineOfSight(
		const UWorld* World,
		const FVector& StartLocation,
		const FVector& TargetLocation,
		const FVector& SurfaceNormal,
		const FCollisionQueryParams& Params);

	/** Wyznacza wektory styczne płaszczyzny dla zadanego kierunku ściany lub podłogi */
	MYPROJECT_API void GetFaceTangents(ESurfaceFaceDirection Face, FVector& OutTangentU, FVector& OutTangentV);

	/** Zwraca prekomputowane 18 kierunków skanowania wybuchu żywiołowego w 3D */
	MYPROJECT_API const TArray<FVector>& GetBurstScanDirections();
}

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"

class UWorld;
class AActor;

/**
 * Zbiór geometrycznych narzędzi projekcji 3D i raycastingu dla siatki powierzchniowej lochu (Surface Grid).
 * 
 * Odpowiada za:
 * - Rzutowanie uderzeń 2D/3D na płaszczyzny ścian i posadzek (ApplyStatusToSurface).
 * - Projekcję fali wybuchu w obszarze 3D z testami Line of Sight (ApplyStatusInArea).
 * - Skanowanie sferyczne wybuchu w 18 kierunkach 3D (ApplyElementalBurst).
 * - Wyszukiwanie komórek stykających się z bryłą kolizyjną aktora w przestrzeni wokselowej (GetCellsTouchingActor).
 */
namespace SurfaceGridProjectionUtils
{
	using FSurfaceCellCandidateCallback = TFunctionRef<void(const FSurfaceCellCoord& Coord, EPhysicalMaterialType HitMaterial, AActor* HitActor, const FVector& SurfaceLocation)>;
	using FBurstSurfaceHitCallback = TFunctionRef<void(const FHitResult& SurfaceHit, float SplashRadius)>;

	/**
	 * Wyznacza komórki na powierzchni w zadanym promieniu wokół punktu uderzenia,
	 * sprawdzając drop-off (istnienie geometrii) oraz Line of Sight po powierzchni.
	 * Dla każdej zakwalifikowanej komórki wywołuje callback OnCellCandidate.
	 */
	MYPROJECT_API void ProjectStatusToSurface(
		const UWorld* World,
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		float CellSize,
		const AActor* Instigator,
		FSurfaceCellCandidateCallback OnCellCandidate);

	/**
	 * Wyznacza komórki na powierzchni w obszarze fali wybuchu 3D,
	 * sprawdzając drop-off oraz ścisły Line of Sight 3D z punktu BurstOrigin.
	 * Wykorzystuje InOutProcessedCoords do unikania duplikatów.
	 */
	MYPROJECT_API void ProjectStatusInArea(
		const UWorld* World,
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		const FVector& BurstOrigin,
		float CellSize,
		TSet<FSurfaceCellCoord>& InOutProcessedCoords,
		const AActor* Instigator,
		FSurfaceCellCandidateCallback OnCellCandidate);

	/**
	 * Filtruje istniejące aktywne komórki znajdujące się w sferze wybuchu,
	 * weryfikując bezpośredni Line of Sight z BurstOrigin do każdej komórki.
	 */
	MYPROJECT_API void FilterCellsInBurstRadius(
		const UWorld* World,
		const FVector& BurstOrigin,
		float Radius,
		float CellSize,
		const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
		const AActor* Instigator,
		TArray<FSurfaceCellCoord>& OutCellsInRadius);

	/**
	 * Wykonuje promieniowe skanowanie w 18 kierunkach 3D z punktu wybuchu
	 * i dla każdej trafionej architektury lochu wywołuje callback OnSurfaceHit.
	 */
	MYPROJECT_API void ScanBurstSurfaces(
		const UWorld* World,
		const FVector& Origin,
		float Radius,
		float CellSize,
		const AActor* Instigator,
		FBurstSurfaceHitCallback OnSurfaceHit);

	/**
	 * Wyszukuje wszystkie komórki w siatce, z którymi styka się bryła kolizyjna (AABB) aktora.
	 */
	MYPROJECT_API void GetCellsTouchingActor(
		const AActor* Actor,
		const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
		float CellSize,
		TArray<FSurfaceCellCoord>& OutCoords);
}

#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"

class AActor;
class UWorld;
class UStatusEffectComponent;

/**
 * Menedżer statycznej siatki powierzchniowej lochu (ActiveCells).
 * Operuje bezpośrednio w przestrzeni świata (World Space).
 */
class MYPROJECT_API FStaticSurfaceGridManager
{
public:
	FStaticSurfaceGridManager() = default;

	/**
	 * Główny resolver stanu komórki statycznej w siatce świata.
	 */
	bool ApplyStatusToCell(
		UWorld* World,
		const FSurfaceCellCoord& Coord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator,
		EPhysicalMaterialType ExplicitMaterial,
		AActor* SurfaceActor,
		uint8 Tier,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged,
		const FVector& SurfaceLocation = FVector::ZeroVector);


	/**
	 * Usuwa aktywne komórki znajdujące się wewnątrz zadanego prostopadłościanu AABB.
	 */
	int32 ClearCellsInBounds(
		const FBox& BoundingBox,
		float SafeCellSize,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Wygasza przeterminowane statusy w komórkach statycznych.
	 */
	void ExpireCells(float CurrentTime, TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Rozprzestrzenia stały ogień po materiale stanowiącym paliwo (bSelfSustainingFuel).
	 */
	void ProcessSolidFuelCombustion(
		UWorld* World,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Przetwarza interakcję pojedynczego aktora ze stykającymi się komórkami statycznymi.
	 */
	void ProcessActorInteraction(
		AActor* Actor,
		UStatusEffectComponent* StatusComp,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<bool(const FSurfaceCellCoord&, EStatusEffectType, float, AActor*)> ApplyToCellCallback);

	/**
	 * Pobiera materiał fizyczny, aktora lochu oraz dokładną pozycję uderzenia pod daną komórką powierzchniową.
	 */
	static bool GetSurfaceMaterialAtCoord(
		const UWorld* World,
		float SafeCellSize,
		const FSurfaceCellCoord& Coord,
		EPhysicalMaterialType& OutMaterial,
		AActor*& OutSurfaceActor,
		FVector& OutSurfaceLocation);

	static bool GetSurfaceMaterialAtCoord(
		const UWorld* World,
		float SafeCellSize,
		const FSurfaceCellCoord& Coord,
		EPhysicalMaterialType& OutMaterial,
		AActor*& OutSurfaceActor)
	{
		FVector DummyLocation;
		return GetSurfaceMaterialAtCoord(World, SafeCellSize, Coord, OutMaterial, OutSurfaceActor, DummyLocation);
	}

	/** Dostęp do mapy komórek */
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& GetActiveCells() const { return ActiveCells; }

	/** Rysowanie debugowe */
	void DrawDebug(const UWorld* World, float SafeCellSize) const;

private:
	/** Rzadka mapa aktywnych komórek powierzchniowych (World Space) */
	TMap<FSurfaceCellCoord, FSurfaceCellData> ActiveCells;
};

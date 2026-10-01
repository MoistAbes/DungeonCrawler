#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"

class USceneComponent;
class AActor;
class UWorld;
class UStatusEffectComponent;

/**
 * Kontener na komórki powierzchniowe pojedynczego dynamicznego aktora/mechanizmu.
 * Przechowuje komórki w lokalnej przestrzeni współrzędnych (Local Space) wyznaczonego komponentu sceny.
 */
struct FDynamicActorSurfaceGrid
{
	TWeakObjectPtr<AActor> OwnerActor = nullptr;
	TWeakObjectPtr<USceneComponent> TransformComponent = nullptr;

	/** Rzadka mapa komórek w lokalnej przestrzeni współrzędnych komponentu */
	TMap<FSurfaceCellCoord, FSurfaceCellData> LocalCells;
};

/**
 * Menedżer dynamicznych siatek powierzchniowych powiązanych z ruchomymi aktorami (bramy, windy itp.).
 * Operuje w lokalnej przestrzeni współrzędnych komponentów (Local Space).
 */
class MYPROJECT_API FDynamicSurfaceGridManager
{
public:
	FDynamicSurfaceGridManager() = default;

	/** Kwalifikuje i zwraca komponent transformacji dla dynamicznego aktora */
	static USceneComponent* GetDynamicActorTransformComponent(AActor* Actor);

	/** Zwraca sztywną transformację komponentu sceny (translacja + rotacja, skala 1.0) */
	static FTransform GetDynamicRigidTransform(const USceneComponent* Comp);

	/**
	 * Nakłada status na powierzchnię pojedynczego dynamicznego aktora w jego przestrzeni lokalnej.
	 * @return Liczba zmodyfikowanych komórek.
	 */
	int32 ApplyStatusToDynamicSurface(
		AActor* DynamicActor,
		USceneComponent* TransformComp,
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator,
		uint8 Tier,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Nakłada status na komórkę w lokalnej siatce dynamicznego aktora.
	 * @return true jeśli komórka została zmodyfikowana.
	 */
	bool ApplyStatusToDynamicCell(
		AActor* DynamicActor,
		USceneComponent* TransformComp,
		const FSurfaceCellCoord& LocalCoord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator,
		EPhysicalMaterialType ExplicitMaterial,
		uint8 Tier,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/** Usuwa komórki dynamiczne przecinające zadany prostopadłościan AABB */
	int32 ClearCellsInBounds(const FBox& BoundingBox, float SafeCellSize);

	/** Wygasza przeterminowane statusy w komórkach dynamicznych oraz czyści niepoprawnych aktorów */
	void ExpireCells(float CurrentTime, TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/** Przetwarza interakcje postaci z komórkami dynamicznymi */
	void ProcessActorInteractions(AActor* Actor, UStatusEffectComponent* StatusComp, float SafeCellSize);

	/** Dostęp do siatek */
	const TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& GetGrids() const { return DynamicSurfaceGrids; }
	TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& GetGridsMutable() { return DynamicSurfaceGrids; }

	/** Rysowanie debugowe */
	void DrawDebug(const UWorld* World, float SafeCellSize) const;

private:
	/** Rejestr dynamicznych siatek powierzchniowych powiązanych z ruchomymi aktorami (Local Space) */
	TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid> DynamicSurfaceGrids;
};

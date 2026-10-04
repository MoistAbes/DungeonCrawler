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

	/** Konwertuje współrzędne i normalną świata na lokalną komórkę FSurfaceCellCoord komponentu */
	static FSurfaceCellCoord WorldToLocalCoord(
		const USceneComponent* Comp,
		const FVector& WorldLocation,
		const FVector& WorldNormal,
		float SafeCellSize);

	/** Konwertuje światową komórkę FSurfaceCellCoord na lokalną komórkę FSurfaceCellCoord komponentu */
	FORCEINLINE static FSurfaceCellCoord WorldToLocalCoord(
		const USceneComponent* Comp,
		const FSurfaceCellCoord& WorldCoord,
		float SafeCellSize)
	{
		return WorldToLocalCoord(
			Comp,
			WorldCoord.ToWorldLocation(SafeCellSize),
			SurfaceGridUtils::FaceDirectionToNormal(WorldCoord.Face),
			SafeCellSize);
	}

	/** Konwertuje lokalną komórkę FSurfaceCellCoord na środek woksela w przestrzeni świata */
	static FVector LocalToWorldLocation(
		const USceneComponent* Comp,
		const FSurfaceCellCoord& LocalCoord,
		float SafeCellSize);

	/** Konwertuje lokalną komórkę FSurfaceCellCoord na punkt styku powierzchni w przestrzeni świata */
	static FVector LocalToWorldSurfaceContact(
		const USceneComponent* Comp,
		const FSurfaceCellCoord& LocalCoord,
		float SafeCellSize);

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
	 * Sprawdza komórki dynamiczne w sferze wybuchu i aplikuje status z testem widoczności Line-of-Sight.
	 * @return Liczba zmodyfikowanych komórek dynamicznych.
	 */
	int32 ApplyElementalBurst(
		UWorld* World,
		const FVector& Origin,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator,
		uint8 Tier,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged,
		int32* OutNewlyAddedCount = nullptr);

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
	int32 ClearCellsInBounds(
		const FBox& BoundingBox,
		float SafeCellSize,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/** Wygasza przeterminowane statusy w komórkach dynamicznych oraz czyści niepoprawnych aktorów */
	void ExpireCells(float CurrentTime, TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/** Przetwarza dwukierunkowe interakcje postaci z komórkami dynamicznymi (Faza A: Postać -> Komórka, Faza B: Komórka -> Postać) */
	void ProcessActorInteractions(
		AActor* Actor,
		UStatusEffectComponent* StatusComp,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/** Dostęp do siatek */
	const TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& GetGrids() const { return DynamicSurfaceGrids; }
	TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& GetGridsMutable() { return DynamicSurfaceGrids; }

	/** Rysowanie debugowe */
	void DrawDebug(const UWorld* World, float SafeCellSize) const;

private:
	/** Rejestr dynamicznych siatek powierzchniowych powiązanych z ruchomymi aktorami (Local Space) */
	TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid> DynamicSurfaceGrids;
};

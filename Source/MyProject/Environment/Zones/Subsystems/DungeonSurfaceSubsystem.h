#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "MyProject/Environment/Zones/Managers/StaticSurfaceGridManager.h"
#include "MyProject/Environment/Zones/Managers/DynamicSurfaceGridManager.h"
#include "DungeonSurfaceSubsystem.generated.h"

class ACharacter;
class UStatusEffectComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSurfaceCellChanged, const FSurfaceCellCoord&, Coord, EStatusEffectType, NewStatus, AActor*, Instigator);

/**
 * Podsystem świata zarządzający rzadką siatką komórek powierzchniowych (Sparse World Surface Grid).
 * 
 * Pełni rolę architektonicznej Fasady (Facade Pattern) i orkiestratora:
 * - Koordynuje statyczną siatkę świata (FStaticSurfaceGridManager) i dynamiczne siatki mechanizmów (FDynamicSurfaceGridManager).
 * - Realizuje cykliczny tick serwera: wygaszanie, dwukierunkowy automat komórkowy (FSurfaceGridPropagationUtils),
 *   niszczenie architektury lochu, samozapłon paliwa i interakcję z postaciami.
 * - Udostępnia publiczne, autorytatywne API (Single Point of Truth) dla pocisków, wybuchów i interakcji.
 */
UCLASS()
class MYPROJECT_API UDungeonSurfaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDungeonSurfaceSubsystem();

	// --- Cykl Życia Subsystemu ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Fizyczny rozmiar pojedynczej komórki w centymetrach (domyślnie 50 cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float CellSize = 50.0f;

	/** Interwał serwera sprawdzający obecność postaci na aktywnych komórkach i wygaszanie (w sekundach) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|SurfaceGrid", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SubsystemTickInterval = 0.25f;

	/** Flaga włączająca debugowe rysowanie aktywnych komórek w edytorze i trybach deweloperskich */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Debug")
	bool bDrawDebugGrid = true;

	// -------------------------------------------------------------------------
	// Główne API Domenowe (Fasada)
	// -------------------------------------------------------------------------

	/**
	 * Kwalifikuje, czy dany aktor może być podłożem pod komórki powierzchniowe.
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|SurfaceGrid")
	static bool IsValidSurfaceTarget(const AActor* Actor);

	/**
	 * Rozwiązuje uderzenie żywiołem w świecie (Hit Resolver):
	 * - Dynamiczny mechanizm (np. brama, winda) -> kieruje do ApplyStatusToDynamicSurface.
	 * - Statyczna architektura (podłoga, ściana) -> kieruje do ApplyStatusToSurface.
	 * 
	 * @return Liczba zmodyfikowanych komórek powierzchniowych.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyStatusFromHit(
		const FHitResult& HitResult,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * Nakłada status na komórki pojedynczej powierzchni statycznej wokół punktu uderzenia.
	 * 
	 * @return Liczba zmodyfikowanych lub zaktualizowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyStatusToSurface(
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * JEDYNY ATOMOWY PUNKT STYKU (Single Point of Truth) dla stanu komórki w siatce statycznej.
	 * 
	 * @return true jeśli komórka została zmodyfikowana (dodana, odświeżona, przereagowana lub usunięta).
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	bool ApplyStatusToCell(
		const FSurfaceCellCoord& Coord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator = nullptr,
		EPhysicalMaterialType ExplicitMaterial = EPhysicalMaterialType::Stone,
		AActor* SurfaceActor = nullptr,
		uint8 Tier = 0);

	/**
	 * Nakłada status na komórki w obszarze 3D wokół źródła wybuchu (RadialBurst).
	 */
	int32 ApplyStatusInArea(
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		const FVector& BurstOrigin,
		TSet<FSurfaceCellCoord>& ProcessedCoords,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * Nakłada status na powierzchnię pojedynczego dynamicznego aktora w jego przestrzeni lokalnej.
	 * 
	 * @return Liczba zmodyfikowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyStatusToDynamicSurface(
		AActor* DynamicActor,
		USceneComponent* TransformComp,
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * Nakłada status na komórkę w lokalnej siatce dynamicznego aktora.
	 * 
	 * @return true jeśli komórka została zmodyfikowana.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	bool ApplyStatusToDynamicCell(
		AActor* DynamicActor,
		USceneComponent* TransformComp,
		const FSurfaceCellCoord& LocalCoord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator = nullptr,
		EPhysicalMaterialType ExplicitMaterial = EPhysicalMaterialType::Stone,
		uint8 Tier = 0);

	/** Pomocnicza metoda zwracająca komponent transformacji dla dynamicznego aktora */
	UFUNCTION(BlueprintPure, Category = "Custom|SurfaceGrid")
	static USceneComponent* GetDynamicActorTransformComponent(AActor* Actor);

	/** Pomocnicza metoda zwracająca transformację sztywną komponentu (translacja + rotacja, skala 1.0) */
	UFUNCTION(BlueprintPure, Category = "Custom|SurfaceGrid")
	static FTransform GetDynamicRigidTransform(const USceneComponent* Comp);

	/**
	 * Usuwa wszystkie aktywne komórki znajdujące się wewnątrz zadanego prostopadłościanu AABB
	 * (zarówno ze statycznej siatki świata, jak i dynamicznych obiektów).
	 * 
	 * @return Liczba usuniętych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ClearCellsInBounds(const FBox& BoundingBox);

	/**
	 * Aplikuje impuls żywiołowy w sferze o zadanym promieniu (wybuch beczki, czar obszarowy).
	 * 
	 * @return Liczba zaktualizowanych lub pomalowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyElementalBurst(
		const FVector& Origin,
		float Radius,
		EStatusEffectType Status,
		float Duration = 5.0f,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/** Zdarzenie wywoływane przy zmianie stanu komórki (podstawa dla systemów VFX/SFX) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnSurfaceCellChanged OnSurfaceCellChanged;

	/** Rejestruje komponent statusów do cyklicznej ewaluacji z siatką powierzchni */
	void RegisterStatusComponent(UStatusEffectComponent* Comp);

	/** Wyrejestrowuje komponent statusów z podsystemu */
	void UnregisterStatusComponent(UStatusEffectComponent* Comp);

	/** Dostęp do menedżerów składowych */
	const FStaticSurfaceGridManager& GetStaticGridManager() const { return StaticGridManager; }
	FStaticSurfaceGridManager& GetStaticGridManager() { return StaticGridManager; }
	const FDynamicSurfaceGridManager& GetDynamicGridManager() const { return DynamicGridManager; }
	FDynamicSurfaceGridManager& GetDynamicGridManager() { return DynamicGridManager; }

	/** Dostęp do danych dla kompatybilności wstecznej */
	const TMap<FSurfaceCellCoord, FSurfaceCellData>& GetActiveCells() const { return StaticGridManager.GetActiveCells(); }
	const TMap<TWeakObjectPtr<AActor>, FDynamicActorSurfaceGrid>& GetDynamicSurfaceGrids() const { return DynamicGridManager.GetGrids(); }

	/** Metody pomocnicze ustalania materiału podłożowego */
	bool GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial) const;
	bool GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial, AActor*& OutSurfaceActor) const;

protected:
	/** Okresowa pętla serwera: wygaszanie, propagacja, obrażenia i aplikacja statusów na postacie */
	UFUNCTION()
	void ProcessGridTick();

	/** Rysuje debugowe wizualizacje aktywnych komórek w świecie */
	void DrawDebugVisuals() const;

private:
	/** Ewaluuje dwukierunkową interakcję z zarejestrowanymi komponentami statusów */
	void ProcessActorInteractions(float CurrentTime);

	/** Rejestr aktywnych komponentów statusów w świecie podlegających interakcji z podłożem */
	UPROPERTY()
	TArray<TWeakObjectPtr<UStatusEffectComponent>> RegisteredStatusComponents;

	/** Menedżer komórek statycznej siatki świata */
	FStaticSurfaceGridManager StaticGridManager;

	/** Menedżer komórek dynamicznych mechanizmów lochu */
	FDynamicSurfaceGridManager DynamicGridManager;

	/** Uchwyt timera serwerowego */
	FTimerHandle GridTickTimerHandle;
};

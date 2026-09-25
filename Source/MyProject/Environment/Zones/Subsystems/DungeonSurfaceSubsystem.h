#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "DungeonSurfaceSubsystem.generated.h"

class ACharacter;
class UStatusEffectComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSurfaceCellChanged, const FSurfaceCellCoord&, Coord, EStatusEffectType, NewStatus, AActor*, Instigator);

/**
 * Podsystem świata zarządzający rzadką siatką komórek powierzchniowych (Sparse World Surface Grid).
 * 
 * Zastępuje analityczne obrysy radialne stref powierzchniowych.
 * Odpowiada za:
 * - Przechowywanie aktywnych komórek w pamięci podręcznej (TMap) bez alokacji osobnych aktorów.
 * - Mapowanie uderzeń cieczy/ognia na dyskretne komórki z uwzględnieniem strony fundamentu (Face Direction).
 * - Ewaluację reakcji chemicznych między żywiołami (np. Ogień + Olej = Płomień).
 * - Błyskawiczne usuwanie komórek z obszaru zniszczonych fundamentów (ClearCellsInBounds).
 * - Cykliczną aplikację statusów na wchodzące postacie oraz wygaszanie przeterminowanych komórek.
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
	// Główne API Domenowe
	// -------------------------------------------------------------------------

	/**
	 * Kwalifikuje, czy dany aktor może być podłożem pod komórki powierzchniowe.
	 * Akceptuje fundamenty lochu (ADungeonStructureBase) oraz geometrię poziomu (ABrush).
	 * Odrzuca dynamiczne rekwizyty (AInteractivePropBase) oraz postacie (APawn).
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|SurfaceGrid")
	static bool IsValidSurfaceTarget(const AActor* Actor);

	/**
	 * Rozwiązuje uderzenie żywiołem w świecie (Hit Resolver):
	 * - Trafienie w postać/rekwizyt: nakłada status na cel i szuka podłogi pod jego stopami (FloorTrace), malując podłoże.
	 * - Trafienie w strefę przestrzenną (np. chmurę): przekazuje trafienie żywiołowe strefie.
	 * - Trafienie w ścianę/podłogę: weryfikuje podłoże i wywołuje PaintSurface.
	 * 
	 * @return Liczba pomalowanych komórek powierzchniowych.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 PaintSurfaceFromHit(
		const FHitResult& HitResult,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr);

	/**
	 * Maluje strefę żywiołu na powierzchniach wokół punktu uderzenia.
	 * Wyznacza komórki w promieniu Radius, uwzględnia orientację ściany/podłogi
	 * i przeprowadza ewaluację reakcji chemicznych z istniejącymi na nich statusami.
	 * 
	 * @return Liczba pomalowanych lub zaktualizowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 PaintSurface(
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr);

	/**
	 * JEDYNY ATOMOWY PUNKT STYKU (Single Point of Truth) dla stanu komórki w siatce.
	 * Przyjmuje status z dowolnego źródła (wybuch, plama, pocisk, postać, propagacja),
	 * odpytuje reguły UElementalReactionRules, aplikuje wynik reakcji i zarządza ActiveCells.
	 *
	 * @return true jeśli komórka została zmodyfikowana (dodana, odświeżona, przereagowana lub usunięta).
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	bool ApplyStatusToCell(
		const FSurfaceCellCoord& Coord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator = nullptr,
		EPhysicalMaterialType ExplicitMaterial = EPhysicalMaterialType::Stone);

	/**
	 * Wewnętrzna wersja metody PaintSurface z możliwością przekazania zbioru przetworzonych koordynatów
	 * w ramach jednego złożonego zdarzenia (np. wybuchu 3D) dla uniknięcia wielokrotnego przetwarzania kafelka.
	 */
	int32 PaintSurfaceInternal(
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr,
		TSet<FSurfaceCellCoord>* ProcessedCoords = nullptr);

	/**
	 * Usuwa wszystkie aktywne komórki znajdujące się wewnątrz zadanego prostopadłościanu AABB.
	 * Wywoływane automatycznie przez ADungeonStructureBase w momencie zniszczenia ściany lub podłogi.
	 * 
	 * @return Liczba usuniętych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ClearCellsInBounds(const FBox& BoundingBox);

	/**
	 * Aplikuje impuls żywiołowy w sferze o zadanym promieniu (wybuch beczki, czar obszarowy).
	 * Wywołuje reakcje chemiczne ze wszystkimi komórkami w zasięgu oraz opcjonalnie maluje strefę na trafionej posadzce.
	 * 
	 * @return Liczba zaktualizowanych lub pomalowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyElementalBurst(
		const FVector& Origin,
		float Radius,
		EStatusEffectType Status,
		float Duration = 5.0f,
		AActor* Instigator = nullptr);

	/** Zdarzenie wywoływane przy zmianie stanu komórki (podstawa dla systemów VFX/SFX) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnSurfaceCellChanged OnSurfaceCellChanged;

	/** Rejestruje komponent statusów do cyklicznej ewaluacji z siatką powierzchni */
	void RegisterStatusComponent(UStatusEffectComponent* Comp);

	/** Wyrejestrowuje komponent statusów z podsystemu */
	void UnregisterStatusComponent(UStatusEffectComponent* Comp);

protected:
	/** Okresowa pętla serwera: wygaszanie starych komórek i aplikacja statusów na postacie */
	UFUNCTION()
	void ProcessGridTick();

	/** Rysuje debugowe wizualizacje aktywnych komórek w świecie */
	void DrawDebugVisuals() const;

private:
	/** Zwraca współrzędne wszystkich komórek, z którymi w danej chwili styka się bryła kolizyjna lub stopy aktora */
	void GetCellsTouchingActor(const AActor* Actor, TArray<FSurfaceCellCoord>& OutCoords) const;

	/** Pobiera materiał fizyczny architektury lochu pod daną komórką powierzchniową. Zwraca false jeśli brak fizycznej geometrii. */
	bool GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial) const;

	/** Rejestr aktywnych komponentów statusów w świecie podlegających interakcji z podłożem */
	UPROPERTY()
	TArray<TWeakObjectPtr<UStatusEffectComponent>> RegisteredStatusComponents;

	/** Rzadka mapa aktywnych komórek powierzchniowych */
	TMap<FSurfaceCellCoord, FSurfaceCellData> ActiveCells;

	/** Uchwyt timera serwerowego */
	FTimerHandle GridTickTimerHandle;
};

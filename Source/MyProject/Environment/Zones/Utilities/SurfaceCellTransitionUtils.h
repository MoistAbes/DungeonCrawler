#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "SurfaceCellTransitionUtils.generated.h"

/**
 * Narzędzie domenowe odpowiedzialne za aplikację przejść stanów żywiołowych
 * do komórek rzadkiej siatki powierzchniowej (Surface Grid).
 * 
 * Pośredniczy między abstrakcyjnym silnikiem praw żywiołów (UElementalReactionRules),
 * który generuje plan transformacji chemicznej (FElementalTransitionPlan),
 * a fizyczną reprezentacją danych komórki (FSurfaceCellData).
 */
UCLASS()
class MYPROJECT_API USurfaceCellTransitionUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Główny resolver stanu komórki powierzchniowej.
	 * Wylicza całkowity nowy stan komórki (reakcje, nośniki, wygaszanie, tożsamość materiałowa).
	 * Modyfikuje InOutCellData i zwraca raport z przejścia stanu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	static FSurfaceCellTransitionResult CalculateCellTransition(
		UPARAM(ref) FSurfaceCellData& InOutCellData,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator,
		float CurrentTime,
		uint8 Tier = 0);

	/** Usuwa z komórki statusy, które bez swoich nośników nie mogą dłużej legalnie istnieć na tym materiale */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	static bool CleanOrphanedStatuses(
		UPARAM(ref) FSurfaceCellData& InOutCellData,
		EStatusEffectType StatusToPreserve = EStatusEffectType::None);

	/**
	 * Atomowa aplikacja statusu do komórki w dowolnej rzadkiej mapie komórek (zarówno statycznej, jak i dynamicznej).
	 * Wylicza przejście stanu chemicznego, inicjalizuje paliwo stałe, modyfikuje mapę i powiadamia o zmianie stanu.
	 * 
	 * @return true jeśli komórka została zmodyfikowana.
	 */
	static bool ApplyStatusToCellInMap(
		TMap<FSurfaceCellCoord, FSurfaceCellData>& CellMap,
		const FSurfaceCellCoord& Coord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator,
		EPhysicalMaterialType ExplicitMaterial,
		AActor* SurfaceActor,
		uint8 Tier,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Uniwersalna pętla wygaszania komórek w dowolnej rzadkiej mapie komórek (statycznej lub dynamicznej).
	 * Wygasza przeterminowane statusy, oczyszcza osierocone nośniki, usuwa puste komórki i wywołuje callback OnCellChanged.
	 */
	static void ExpireCellsInMap(
		TMap<FSurfaceCellCoord, FSurfaceCellData>& CellMap,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Zwraca kolor debugowy odpowiadający dominującemu stanowi komórki (oraz stanom złożonym np. Wet + Electrified).
	 */
	static FColor GetCellDebugColor(const FSurfaceCellData& CellData);
};

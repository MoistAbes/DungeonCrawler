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
};

#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"

class AActor;
class UStatusEffectComponent;

/**
 * Narzędzie domenowe zarządzające dwukierunkową interakcją żywiołową
 * pomiędzy aktorami świata (np. gracz, stwory, skrzynki) a siatką komórek podłoża (Surface Grid).
 * 
 * Odpowiada za:
 * - Faza A (Actor -> Floor): Płonący aktor podpala stykające się kałuże oleju, gasi wodą itp.
 * - Faza B (Floor -> Actor): Podłoże nakłada na aktora dominującą ciecz (woda/olej) oraz statusy wtórne (ogień/prąd).
 */
namespace SurfaceActorInteractionUtils
{
	using FApplyStatusToCellFunc = TFunctionRef<bool(const FSurfaceCellCoord& Coord, EStatusEffectType Status, float Duration, AActor* Instigator)>;

	/**
	 * Faza A: Aktor wpływa na stykające się komórki podłoża.
	 * Ewaluuje reakcje statusów aktora ze statusem komórki podłoża, zużywa statusy aktora jeśli reakcja tego wymaga
	 * i aplikuje wynikowy status do komórki za pośrednictwem ApplyStatusToCellFunc.
	 */
	MYPROJECT_API void ApplyActorEffectsToFloor(
		AActor* Actor,
		UStatusEffectComponent* StatusComp,
		const TArray<FSurfaceCellCoord>& TouchedCells,
		TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
		TArray<EStatusEffectType>& InOutActorStatuses,
		FApplyStatusToCellFunc ApplyStatusToCell);

	/**
	 * Faza B: Podłoże wpływa na aktora.
	 * Zgodnie ze złotą zasadą chemiczną:
	 * 1. Najpierw aplikuje JEDEN dominujący płyn (z najbliższej dotkniętej komórki płynnej).
	 * 2. Następnie aplikuje pozostałe statusy środowiskowe (np. ogień, prąd) o najwyższym tierze.
	 */
	MYPROJECT_API void ApplyFloorEffectsToActor(
		AActor* Actor,
		UStatusEffectComponent* StatusComp,
		const TArray<FSurfaceCellCoord>& TouchedCells,
		const TMap<FSurfaceCellCoord, FSurfaceCellData>& ActiveCells,
		float CellSize);
}

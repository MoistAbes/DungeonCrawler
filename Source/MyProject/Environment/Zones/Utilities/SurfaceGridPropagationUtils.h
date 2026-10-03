#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"

class FStaticSurfaceGridManager;
class FDynamicSurfaceGridManager;
class AActor;
class UWorld;

/**
 * Bezstanowe narzędzie domenowe odpowiedzialne za ewolucję automatu komórkowego (Cellular Automata).
 * Realizuje dwukierunkowe rozprzestrzenianie statusów:
 * - Statyczna siatka świata <-> Statyczna siatka świata
 * - Statyczna siatka świata <-> Dynamiczne siatki ruchomych mechanizmów
 * - Dynamiczna siatka mechanizmu <-> Dynamiczna siatka mechanizmu
 *
 * Wszystkie reguły propagacji i filtrowania są w 100% data-driven
 * (odpytują UElementalReactionRules bez zahardkodowanych enumów if(Wet) czy if(Burning)).
 */
class MYPROJECT_API FSurfaceGridPropagationUtils
{
public:
	/**
	 * Główna pętla propagacji żywiołów (Cellular Automata).
	 */
	static void PropagateElementalSpreads(
		UWorld* World,
		FStaticSurfaceGridManager& StaticGrid,
		FDynamicSurfaceGridManager& DynamicGrid,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);

	/**
	 * Błyskawiczna propagacja sieci przewodzącej (Instant Conduction Network / Flood-Fill).
	 * Rozprzestrzenia statusy o fladze bInstantConductionNetwork (np. Electrified)
	 * natychmiastowo po wszystkich połączonych komórkach przewodzących (woda, metal)
	 * ze zsynchronizowanym wspólnym ServerEndTime.
	 */
	static void PropagateConductionNetworks(
		UWorld* World,
		FStaticSurfaceGridManager& StaticGrid,
		FDynamicSurfaceGridManager& DynamicGrid,
		float SafeCellSize,
		float CurrentTime,
		TFunctionRef<void(const FSurfaceCellCoord&, EStatusEffectType, AActor*)> OnCellChanged);
};

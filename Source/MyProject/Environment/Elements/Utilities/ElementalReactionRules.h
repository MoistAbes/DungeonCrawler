#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "ElementalReactionRules.generated.h"

/**
 * Główny silnik praw żywiołów świata gry (Elemental Reaction Rules Engine).
 * Odpowiada za:
 * - Weryfikację kompatybilności tożsamości materiałowej na podstawie cech fizycznych (bFlammable, bConductive).
 * - Ewaluację interakcji i reakcji łańcuchowych między żywiołami.
 * - Udostępnianie kart konfiguracyjnych poszczególnych statusów (obrażenia DoT, właściwości płynów).
 * - Autorytatywne wyliczanie przejść stanów dla komórek powierzchniowych i obiektów.
 */
UCLASS()
class MYPROJECT_API UElementalReactionRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Zwraca kompletną specyfikację danego statusu (obrażenia, czasy, reguły) */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static const FStatusEffectConfig& GetEffectConfig(EStatusEffectType Status);

	/** Sprawdza, czy dany status jest płynem (wypiera inne płyny z powierzchni) */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static bool IsLiquidStatus(EStatusEffectType Status);

	/**
	 * Sprawdza, czy obiekt o danym materiale fizycznym może przyjąć przychodzący status,
	 * odpytując cechy fizyczne materiału (bFlammable, bConductive) oraz obecne powłoki celu (np. Oiled, Wet).
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static bool CanMaterialReceiveStatus(
		EPhysicalMaterialType Material,
		EStatusEffectType IncomingStatus,
		const TArray<EStatusEffectType>& ActiveStatuses);

	/**
	 * Dokonuje ewaluacji reakcji chemicznej na podstawie listy obecnych statusów celu
	 * oraz przychodzącego nowego żywiołu.
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static FElementalReactionResult EvaluateReaction(
		EStatusEffectType IncomingStatus,
		const TArray<EStatusEffectType>& ActiveStatuses);

	/**
	 * Sprawdza, czy dany status może rozprzestrzenić się na sąsiednią komórkę w siatce (np. ogień na olej).
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static bool CanSpreadToNeighbor(
		EStatusEffectType SourceStatus,
		EStatusEffectType TargetStatus,
		FElementalReactionResult& OutReactionResult);

	/** Zwraca wagę pierwszeństwa reakcji chemicznej dla danego statusu */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static int32 GetReactionPriority(EStatusEffectType Status);

	/** Sortuje listę statusów malejąco według priorytetu reakcji żywiołowych */
	UFUNCTION(BlueprintCallable, Category = "Custom|Elemental")
	static void SortByReactionPriority(TArray<EStatusEffectType>& InOutStatuses);

	/** Sprawdza, czy status zależny synchronizuje swój czas z nośnikiem (zgodnie z konfiguracją reguł w DataAsset) */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static bool DoesStatusSyncWithCarrier(EStatusEffectType DependentStatus, EStatusEffectType CarrierStatus);

	/**
	 * Sprawdza, czy dany materiał bez żadnych aktywnych nośników/powłok wymaga nośnika,
	 * aby w ogóle móc utrzymać ten status (np. Stone dla Burning lub Electrified).
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static bool RequiresCarrierToSustain(EPhysicalMaterialType Material, EStatusEffectType Status);

	/**
	 * Główny resolver stanu komórki powierzchniowej.
	 * Wylicza całkowity nowy stan komórki (reakcje, nośniki, wygaszanie, tożsamość materiałowa).
	 * Modyfikuje InOutCellData i zwraca raport z przejścia stanu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|Elemental")
	static FSurfaceCellTransitionResult CalculateCellTransition(
		UPARAM(ref) FSurfaceCellData& InOutCellData,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator,
		float CurrentTime,
		uint8 Tier = 0);

	/** Usuwa z komórki statusy, które bez swoich nośników nie mogą dłużej legalnie istnieć na tym materiale */
	UFUNCTION(BlueprintCallable, Category = "Custom|Elemental")
	static bool CleanOrphanedStatuses(UPARAM(ref) FSurfaceCellData& InOutCellData, EStatusEffectType StatusToPreserve = EStatusEffectType::None);

private:
	static const TMap<EStatusEffectType, FStatusEffectConfig>& GetConfigRegistry();
};

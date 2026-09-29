#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
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

	/** Sprawdza, czy dany status na danym materiale jest permanentny (np. Burning na materiale bSelfSustainingFuel) */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static bool IsPermanentStatus(EPhysicalMaterialType Material, EStatusEffectType Status);

	/**
	 * Uniwersalny resolver czasu trwania statusu zależnego od nośnika (Single Source of Truth).
	 * Oblicza skorygowany czas trwania na podstawie właściwości materiału celu oraz obecności nośnika.
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static float ApplyCarrierSync(
		EPhysicalMaterialType Material,
		EStatusEffectType Status,
		float CurrentDuration,
		EStatusEffectType CarrierStatus,
		float CarrierRemainingTime,
		bool bSyncWithCarrier);

	/**
	 * Centralny, uniwersalny kalkulator przejścia stanu elementarnego (Single Source of Truth).
	 * Wykonuje pełny łańcuch reakcji żywiołowych, pętlę równowagi chemicznej, wyparcie płynów oraz czyszczenie osieroconych statusów.
	 * Wykorzystywany zarówno przez komórki siatki podłoża, jak i komponenty aktorów (UStatusEffectComponent).
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	static FElementalTransitionPlan CalculateElementalTransition(
		EPhysicalMaterialType TargetMaterial,
		EStatusEffectType IncomingStatus,
		float IncomingDuration,
		uint8 IncomingTier,
		const TArray<FElementalActiveStatusSnapshot>& CurrentStatuses);

private:
	static const TMap<EStatusEffectType, FStatusEffectConfig>& GetConfigRegistry();
};

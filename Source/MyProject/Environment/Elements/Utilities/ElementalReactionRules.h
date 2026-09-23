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

private:
	static const TMap<EStatusEffectType, FStatusEffectConfig>& GetConfigRegistry();
};

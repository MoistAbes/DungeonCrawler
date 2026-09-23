#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "StatusEffectTypes.generated.h"

/**
 * Reguła reakcji żywiołowej.
 * Definiuje co się dzieje, gdy przychodzący status uderza w aktywny status celu/komórki.
 */
USTRUCT(BlueprintType)
struct FStatusReactionRule
{
	GENERATED_BODY()

	/** Czy przychodzący status ulega zużyciu/neutralizacji (np. woda gasząca ogień odparowuje) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	bool bConsumeIncomingStatus = false;

	/** Czy istniejący na celu status zostaje usunięty/wygaszony (np. ogień gaśnie od wody) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	bool bRemoveExistingStatus = false;

	/** Natychmiastowe obrażenia bonusowe wywołane reakcją (np. wybuch oleju, szok elektryczny) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	float BonusInstantDamage = 0.0f;

	/** Unikalny identyfikator reakcji (np. "Oil_Ignition", "Steam_Extinguish", "Conductive_Shock") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	FName ReactionTag = NAME_None;

	/** Nowy status powstający w wyniku reakcji (np. Burning po podpaleniu oleju; None przy braku zamiany) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	EStatusEffectType ResultingStatus = EStatusEffectType::None;

	/** Czy ta reakcja może propagować się przestrzennie na sąsiednie komórki w siatce */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	bool bCanSpreadToNeighbor = false;

	/** Czas trwania nowego statusu powstałego w wyniku reakcji */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	float ResultingDuration = 5.0f;
};

/**
 * Niemutowalny wynik ewaluacji reakcji żywiołowej zwracany przez silnik zasad.
 */
USTRUCT(BlueprintType)
struct FElementalReactionResult
{
	GENERATED_BODY()

	/** Czy doszło do jakiejkolwiek interakcji między żywiołami */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	bool bReactionOccurred = false;

	/** Czy przychodzący status uległ zużyciu i nie powinien zostać nałożony na cel */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	bool bConsumeIncomingStatus = false;

	/** Status, który uległ zniszczeniu/wygaszeniu w wyniku reakcji (None jeśli żaden) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	EStatusEffectType ExistingStatusToRemove = EStatusEffectType::None;

	/** Nowy status powstały w wyniku reakcji (None jeśli brak zmiany lub czysta anihilacja) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	EStatusEffectType ResultingStatus = EStatusEffectType::None;

	/** Czy reakcja może rozprzestrzeniać się na sąsiednie komórki */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	bool bCanSpreadToNeighbor = false;

	/** Czas trwania powstałego statusu */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	float ResultingDuration = 0.0f;

	/** Natychmiastowe obrażenia bonusowe wywołane reakcją */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	float BonusInstantDamage = 0.0f;

	/** Identyfikator reakcji (VFX, dźwięk, logi) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Elemental")
	FName ReactionTag = NAME_None;
};

/**
 * Kompletna specyfikacja / karta pojedynczego żywiołu.
 * Czysty model danych uniezależniony od sztywnych list nazw materiałów.
 */
USTRUCT(BlueprintType)
struct FStatusEffectConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	EStatusEffectType EffectType = EStatusEffectType::None;

	/** Czy status jest płynem (wypiera inne płyny z powierzchni) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	bool bIsLiquid = false;

	/** Czy status wymaga materiału z cechą bFlammable (np. Burning) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	bool bRequiresFlammable = false;

	/** Czy status wymaga materiału z cechą bConductive (np. Electrified) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	bool bRequiresConductive = false;

	/** Statusy na celu omijające restrykcję materiałową (np. Oiled omija bRequiresFlammable, Wet omija bRequiresConductive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	TArray<EStatusEffectType> BypassTraitsIfActive;

	/** Obrażenia okresowe zadawane co sekundę (0.0 jeśli brak DoT) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	float DamagePerSecond = 0.0f;

	/** Co ile sekund status aplikuje obrażenia */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	float TickInterval = 1.0f;

	/** Priorytet ewaluacji reakcji (im wyższy, tym wcześniej status wchodzi w reakcje fazowe/anihilacji) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	int32 ReactionPriority = 0;

	/** Dedykowane reguły reakcji z innymi statusami */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Elemental")
	TMap<EStatusEffectType, FStatusReactionRule> Reactions;
};

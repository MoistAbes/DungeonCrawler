#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "ElementalChemistryLibrary.generated.h"

/**
 * Wynik ewaluacji reakcji żywiołowej.
 * Niemutowalne DTO zwracane przez silnik chemii do komponentu statusów.
 */
USTRUCT(BlueprintType)
struct FElementalReactionResult
{
    GENERATED_BODY()

    /** Czy doszło do jakiejkolwiek interakcji/reakcji między żywiołami */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ElementalChemistry")
    bool bReactionOccurred = false;

    /** Czy przychodzący status został całkowicie zneutralizowany (np. ogień odparowany przez wodę) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ElementalChemistry")
    bool bConsumeIncomingStatus = false;

    /** Typ istniejącego statusu, który został zużyty/usunięty przez reakcję (None jeśli żaden) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ElementalChemistry")
    EStatusEffectType ExistingStatusToRemove = EStatusEffectType::None;

    /** Natychmiastowe obrażenia bonusowe wywołane reakcją (np. wybuch oleju, szok elektryczny) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ElementalChemistry")
    float BonusInstantDamage = 0.0f;

    /** Identyfikator reakcji (np. do odtworzenia VFX, dźwięku, logów gameplayowych) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ElementalChemistry")
    FName ReactionTag = NAME_None;
};

/**
 * Domenowy silnik praw chemii świata gry (Chemistry Engine).
 * Odpowiedzialny za:
 * - Weryfikację kompatybilności tożsamości materiałowej ze statusem na podstawie rejestru definicji
 * - Ewaluację interakcji i reakcji łańcuchowych między żywiołami
 */
UCLASS()
class MYPROJECT_API UElementalChemistryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Sprawdza, czy dany status należy do kategorii powłok płynnych (np. Wet, Oiled).
     * Płyny wzajemnie się wykluczają (nowy płyn wypiera poprzedni).
     */
    UFUNCTION(BlueprintPure, Category = "Environment|Elements|Chemistry")
    static bool IsLiquidStatus(EStatusEffectType Status);

    /**
     * Sprawdza, czy obiekt o danym materiale fizycznym może przyjąć przychodzący status,
     * uwzględniając jego obecne stany (np. kamień nie pali się, chyba że jest oblany olejem).
     */
    UFUNCTION(BlueprintPure, Category = "Environment|Elements|Chemistry")
    static bool CanMaterialReceiveStatus(
        EPhysicalMaterialType Material, 
        EStatusEffectType IncomingStatus, 
        const TArray<EStatusEffectType>& ActiveStatuses);

    /**
     * Dokonuje ewaluacji reakcji chemicznej na podstawie listy obecnych statusów celu
     * oraz przychodzącego nowego żywiołu w oparciu o centralny rejestr definicji.
     */
    UFUNCTION(BlueprintPure, Category = "Environment|Elements|Chemistry")
    static FElementalReactionResult EvaluateReaction(
        EStatusEffectType IncomingStatus, 
        const TArray<EStatusEffectType>& ActiveStatuses);
};

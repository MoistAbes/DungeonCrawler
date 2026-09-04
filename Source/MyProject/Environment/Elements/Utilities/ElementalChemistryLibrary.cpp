#include "ElementalChemistryLibrary.h"
#include "MyProject/Environment/Elements/Data/StatusEffectDefinitions.h"

bool UElementalChemistryLibrary::IsLiquidStatus(EStatusEffectType Status)
{
    return FStatusEffectRegistry::GetDefinition(Status).bIsLiquid;
}

bool UElementalChemistryLibrary::CanMaterialReceiveStatus(
    EPhysicalMaterialType Material, 
    EStatusEffectType IncomingStatus, 
    const TArray<EStatusEffectType>& ActiveStatuses)
{
    const FStatusEffectDefinition& Def = FStatusEffectRegistry::GetDefinition(IncomingStatus);

    // 1. Płyny i statusy bez ograniczeń materiałowych mogą oblać dowolną powierzchnię
    if (Def.bIsLiquid || Def.NaturallyAllowedMaterials.Num() == 0)
    {
        return true;
    }

    // 2. Statusy na celu omijające restrykcję materiału (np. Oiled pozwala podpalić kamień/metal)
    for (EStatusEffectType BypassStatus : Def.BypassMaterialIfActive)
    {
        if (ActiveStatuses.Contains(BypassStatus))
        {
            return true;
        }
    }

    // 3. Naturalna kompatybilność materiałowa
    return Def.NaturallyAllowedMaterials.Contains(Material);
}

FElementalReactionResult UElementalChemistryLibrary::EvaluateReaction(
    EStatusEffectType IncomingStatus, 
    const TArray<EStatusEffectType>& ActiveStatuses)
{
    FElementalReactionResult Result;
    const FStatusEffectDefinition& IncomingDef = FStatusEffectRegistry::GetDefinition(IncomingStatus);

    // =========================================================================
    // FAZA 1: Dedykowane reguły reakcji zdefiniowane w karcie przychodzącego żywiołu
    // =========================================================================
    for (EStatusEffectType ActiveStatus : ActiveStatuses)
    {
        if (const FStatusReactionRule* Rule = IncomingDef.Reactions.Find(ActiveStatus))
        {
            Result.bReactionOccurred = true;
            Result.bConsumeIncomingStatus = Rule->bConsumeIncomingStatus;
            Result.ExistingStatusToRemove = Rule->bRemoveExistingStatus ? ActiveStatus : EStatusEffectType::None;
            Result.BonusInstantDamage = Rule->BonusInstantDamage;
            Result.ReactionTag = Rule->ReactionTag;
            return Result;
        }
    }

    // =========================================================================
    // FAZA 2: Reguła Powłok Płynnych (Liquid Displacement / Mutual Exclusivity)
    // Każdy nowy płyn wypiera poprzednio nałożony płyn (np. Wet wypiera Oiled, Oiled wypiera Wet).
    // =========================================================================
    if (IncomingDef.bIsLiquid)
    {
        for (EStatusEffectType ActiveStatus : ActiveStatuses)
        {
            if (ActiveStatus != IncomingStatus && IsLiquidStatus(ActiveStatus))
            {
                Result.bReactionOccurred = true;
                Result.bConsumeIncomingStatus = false; // Nowy płyn nakłada się na cel
                Result.ExistingStatusToRemove = ActiveStatus; // Poprzedni płyn zostaje wyparty/zmyty
                Result.BonusInstantDamage = 0.0f;
                Result.ReactionTag = FName(TEXT("Liquid_Displaced"));
                return Result;
            }
        }
    }

    return Result;
}

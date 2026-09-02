#include "ElementalChemistryLibrary.h"

namespace ElementalChemistryRules
{
    // Naturalna palność materiałów fizycznych
    static bool IsNaturallyFlammable(EPhysicalMaterialType Material)
    {
        return (Material == EPhysicalMaterialType::Wood || 
                Material == EPhysicalMaterialType::Flesh || 
                Material == EPhysicalMaterialType::Default);
    }

    // Naturalna przewodność elektryczna materiałów fizycznych
    static bool IsNaturallyConductive(EPhysicalMaterialType Material)
    {
        return (Material == EPhysicalMaterialType::Metal || 
                Material == EPhysicalMaterialType::Flesh || 
                Material == EPhysicalMaterialType::Default);
    }
}

bool UElementalChemistryLibrary::CanMaterialReceiveStatus(
    EPhysicalMaterialType Material, 
    EStatusEffectType IncomingStatus, 
    const TArray<EStatusEffectType>& ActiveStatuses)
{
    switch (IncomingStatus)
    {
    case EStatusEffectType::Burning:
        // Naoliwiony cel zawsze może stanąć w ogniu (nawet kamień czy metal)
        if (ActiveStatuses.Contains(EStatusEffectType::Oiled))
        {
            return true;
        }
        return ElementalChemistryRules::IsNaturallyFlammable(Material);

    case EStatusEffectType::Electrified:
        // Mokry cel zawsze przewodzi prąd na dowolnej powierzchni
        if (ActiveStatuses.Contains(EStatusEffectType::Wet))
        {
            return true;
        }
        return ElementalChemistryRules::IsNaturallyConductive(Material);

    case EStatusEffectType::Wet:
    case EStatusEffectType::Oiled:
        // Każda powierzchnia fizyczna może zostać oblana cieczą
        return true;

    default:
        return true;
    }
}

FElementalReactionResult UElementalChemistryLibrary::EvaluateReaction(
    EStatusEffectType IncomingStatus, 
    const TArray<EStatusEffectType>& ActiveStatuses)
{
    FElementalReactionResult Result;

    // --- Grupa 1: Przychodzący Ogień (Burning) ---
    if (IncomingStatus == EStatusEffectType::Burning)
    {
        // Reakcja: Ogień uderza w Mokry cel (Steam / Extinguish)
        if (ActiveStatuses.Contains(EStatusEffectType::Wet))
        {
            Result.bReactionOccurred = true;
            Result.bConsumeIncomingStatus = true; // Ogień wyparował
            Result.ExistingStatusToRemove = EStatusEffectType::Wet;
            Result.BonusInstantDamage = 0.0f;
            Result.ReactionTag = FName(TEXT("Steam_Extinguish"));
            return Result;
        }

        // Reakcja: Ogień uderza w Naoliwiony cel (Oil Ignition / Explode)
        if (ActiveStatuses.Contains(EStatusEffectType::Oiled))
        {
            Result.bReactionOccurred = true;
            Result.bConsumeIncomingStatus = false; // Ogień nadal podpala cel!
            Result.ExistingStatusToRemove = EStatusEffectType::Oiled;
            Result.BonusInstantDamage = 25.0f;
            Result.ReactionTag = FName(TEXT("Oil_Ignition"));
            return Result;
        }
    }

    // --- Grupa 2: Przychodząca Woda (Wet) ---
    if (IncomingStatus == EStatusEffectType::Wet)
    {
        // Reakcja: Woda uderza w Płonący cel (Fire Extinguished)
        if (ActiveStatuses.Contains(EStatusEffectType::Burning))
        {
            Result.bReactionOccurred = true;
            Result.bConsumeIncomingStatus = true; // Woda ugasiła pożar
            Result.ExistingStatusToRemove = EStatusEffectType::Burning;
            Result.BonusInstantDamage = 0.0f;
            Result.ReactionTag = FName(TEXT("Fire_Extinguished"));
            return Result;
        }
    }

    // --- Grupa 3: Przychodzący Prąd (Electrified) ---
    if (IncomingStatus == EStatusEffectType::Electrified)
    {
        // Reakcja: Prąd uderza w Mokry cel (Conductive Shock)
        if (ActiveStatuses.Contains(EStatusEffectType::Wet))
        {
            Result.bReactionOccurred = true;
            Result.bConsumeIncomingStatus = false; // Cel nadal zostaje naelektryzowany
            Result.ExistingStatusToRemove = EStatusEffectType::None; // Woda nie znika od razu
            Result.BonusInstantDamage = 15.0f;
            Result.ReactionTag = FName(TEXT("Conductive_Shock"));
            return Result;
        }
    }

    return Result;
}

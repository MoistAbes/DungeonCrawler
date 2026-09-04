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

bool UElementalChemistryLibrary::IsLiquidStatus(EStatusEffectType Status)
{
    return (Status == EStatusEffectType::Wet || 
            Status == EStatusEffectType::Oiled);
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

    default:
        // Płyny mogą oblać dowolną powierzchnię fizyczną
        if (IsLiquidStatus(IncomingStatus))
        {
            return true;
        }
        return true;
    }
}

FElementalReactionResult UElementalChemistryLibrary::EvaluateReaction(
    EStatusEffectType IncomingStatus, 
    const TArray<EStatusEffectType>& ActiveStatuses)
{
    FElementalReactionResult Result;

    // =========================================================================
    // FAZA 1: Gwałtowne Reakcje i Przeciwieństwa Żywiołów (High Priority)
    // =========================================================================

    // --- Przychodzący Ogień (Burning) ---
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

    // --- Przychodząca Woda (Wet) ---
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

    // --- Przychodzący Olej (Oiled) ---
    if (IncomingStatus == EStatusEffectType::Oiled)
    {
        // Reakcja: Olej uderza w Płonący cel (Natychmiastowy zapłon oleju)
        if (ActiveStatuses.Contains(EStatusEffectType::Burning))
        {
            Result.bReactionOccurred = true;
            Result.bConsumeIncomingStatus = true; // Olej natychmiast ulega spaleniu
            Result.ExistingStatusToRemove = EStatusEffectType::None; // Ogień nadal trwa
            Result.BonusInstantDamage = 25.0f;
            Result.ReactionTag = FName(TEXT("Oil_Ignition"));
            return Result;
        }
    }

    // --- Przychodzący Prąd (Electrified) ---
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

    // =========================================================================
    // FAZA 2: Reguła Powłok Płynnych (Liquid Displacement / Mutual Exclusivity)
    // Każdy nowy płyn wypiera poprzednio nałożony płyn (np. Wet wypiera Oiled, Oiled wypiera Wet).
    // =========================================================================
    if (IsLiquidStatus(IncomingStatus))
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

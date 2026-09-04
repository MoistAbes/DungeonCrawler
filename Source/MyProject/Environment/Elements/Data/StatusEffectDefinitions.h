#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"

/**
 * Reguła reakcji żywiołowej:
 * Definiuje co się dzieje, gdy przychodzący status trafia w aktywny status na celu.
 */
struct FStatusReactionRule
{
    /** Czy przychodzący status ulega zużyciu/neutralizacji (np. woda na ogień odparowuje) */
    bool bConsumeIncomingStatus = false;

    /** Czy istniejący na celu status zostaje usunięty (np. ogień gaśnie od wody, olej spala się od ognia) */
    bool bRemoveExistingStatus = false;

    /** Natychmiastowe obrażenia bonusowe wywołane reakcją (wybuch oleju, szok elektryczny) */
    float BonusInstantDamage = 0.0f;

    /** Unikalny identyfikator reakcji (np. "Oil_Ignition", "Steam_Extinguish", "Conductive_Shock") */
    FName ReactionTag = NAME_None;
};

/**
 * Kompletna specyfikacja/karta pojedynczego statusu żywiołowego.
 * Data-Driven definicja przenosząca wiedzę domenową z kodu do pojedynczego, czytelnego wpisu.
 */
struct FStatusEffectDefinition
{
    EStatusEffectType EffectType = EStatusEffectType::None;

    /** Czy dany status należy do kategorii powłok płynnych (np. Wet, Oiled). Płyny wzajemnie się wykluczają. */
    bool bIsLiquid = false;

    /**
     * Lista materiałów fizycznych, na które ten status może naturalnie wejść.
     * Pusta lista oznacza brak ograniczeń materiałowych (status wchodzi na dowolną powierzchnię).
     */
    TArray<EPhysicalMaterialType> NaturallyAllowedMaterials;

    /**
     * Statusy, których obecność na celu omija restrykcję materiałową
     * (np. cel z Oiled może przyjąć Burning nawet na kamieniu lub metalu).
     */
    TArray<EStatusEffectType> BypassMaterialIfActive;

    /** Obrażenia okresowe zadawane co sekundę (0.0 jeśli brak DoT) */
    float DamagePerSecond = 0.0f;

    /** Co ile sekund status aplikuje obrażenia */
    float TickInterval = 1.0f;

    /**
     * Zbiór dedykowanych reguł reakcji z innymi statusami.
     * Klucz: Istniejący na celu status, z którym ten status wchodzi w reakcję.
     */
    TMap<EStatusEffectType, FStatusReactionRule> Reactions;
};

/**
 * Centralny rejestr i baza wiedzy o statusach żywiołowych.
 * Zapewnia dostęp O(1) do definicji dowolnego statusu w jednym wspólnym miejscu.
 */
class FStatusEffectRegistry
{
public:
    static const FStatusEffectDefinition& GetDefinition(EStatusEffectType EffectType)
    {
        static const TMap<EStatusEffectType, FStatusEffectDefinition> Registry = BuildRegistry();
        if (const FStatusEffectDefinition* Found = Registry.Find(EffectType))
        {
            return *Found;
        }

        static const FStatusEffectDefinition EmptyDefinition;
        return EmptyDefinition;
    }

private:
    static TMap<EStatusEffectType, FStatusEffectDefinition> BuildRegistry()
    {
        TMap<EStatusEffectType, FStatusEffectDefinition> Definitions;

        // =====================================================================
        // 1. BURNING (Ogień)
        // =====================================================================
        {
            FStatusEffectDefinition Burning;
            Burning.EffectType = EStatusEffectType::Burning;
            Burning.bIsLiquid = false;
            Burning.NaturallyAllowedMaterials = {
                EPhysicalMaterialType::Wood,
                EPhysicalMaterialType::Flesh,
                EPhysicalMaterialType::Default
            };
            // Naoliwiony cel zawsze może stanąć w ogniu (nawet kamień czy metal)
            Burning.BypassMaterialIfActive = { EStatusEffectType::Oiled };
            Burning.DamagePerSecond = 5.0f;
            Burning.TickInterval = 1.0f;

            // Reakcja: Ogień trafia w Mokry cel (Steam / Extinguish)
            Burning.Reactions.Add(EStatusEffectType::Wet, FStatusReactionRule{
                /* bConsumeIncomingStatus */ true,
                /* bRemoveExistingStatus  */ true,
                /* BonusInstantDamage     */ 0.0f,
                /* ReactionTag            */ FName(TEXT("Steam_Extinguish"))
            });

            // Reakcja: Ogień trafia w Naoliwiony cel (Oil Ignition / Explode)
            Burning.Reactions.Add(EStatusEffectType::Oiled, FStatusReactionRule{
                /* bConsumeIncomingStatus */ false,
                /* bRemoveExistingStatus  */ true,
                /* BonusInstantDamage     */ 25.0f,
                /* ReactionTag            */ FName(TEXT("Oil_Ignition"))
            });

            Definitions.Add(EStatusEffectType::Burning, Burning);
        }

        // =====================================================================
        // 2. WET (Woda / Mokry)
        // =====================================================================
        {
            FStatusEffectDefinition Wet;
            Wet.EffectType = EStatusEffectType::Wet;
            Wet.bIsLiquid = true; // Płyn: wchodzi na każdy materiał i zmywa inne płyny
            Wet.DamagePerSecond = 0.0f;
            Wet.TickInterval = 1.0f;

            // Reakcja: Woda trafia w Płonący cel (Fire Extinguished)
            Wet.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
                /* bConsumeIncomingStatus */ true,
                /* bRemoveExistingStatus  */ true,
                /* BonusInstantDamage     */ 0.0f,
                /* ReactionTag            */ FName(TEXT("Fire_Extinguished"))
            });

            Definitions.Add(EStatusEffectType::Wet, Wet);
        }

        // =====================================================================
        // 3. OILED (Olej / Naoliwiony)
        // =====================================================================
        {
            FStatusEffectDefinition Oiled;
            Oiled.EffectType = EStatusEffectType::Oiled;
            Oiled.bIsLiquid = true; // Płyn: wchodzi na każdy materiał i zmywa inne płyny
            Oiled.DamagePerSecond = 0.0f;
            Oiled.TickInterval = 1.0f;

            // Reakcja: Olej trafia w Płonący cel (Natychmiastowy zapłon oleju)
            Oiled.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
                /* bConsumeIncomingStatus */ true,
                /* bRemoveExistingStatus  */ false,
                /* BonusInstantDamage     */ 25.0f,
                /* ReactionTag            */ FName(TEXT("Oil_Ignition"))
            });

            Definitions.Add(EStatusEffectType::Oiled, Oiled);
        }

        // =====================================================================
        // 4. ELECTRIFIED (Naelektryzowany)
        // =====================================================================
        {
            FStatusEffectDefinition Electrified;
            Electrified.EffectType = EStatusEffectType::Electrified;
            Electrified.bIsLiquid = false;
            Electrified.NaturallyAllowedMaterials = {
                EPhysicalMaterialType::Metal,
                EPhysicalMaterialType::Flesh,
                EPhysicalMaterialType::Default
            };
            // Mokry cel zawsze przewodzi prąd na dowolnej powierzchni
            Electrified.BypassMaterialIfActive = { EStatusEffectType::Wet };
            Electrified.DamagePerSecond = 0.0f;
            Electrified.TickInterval = 1.0f;

            // Reakcja: Prąd trafia w Mokry cel (Conductive Shock)
            Electrified.Reactions.Add(EStatusEffectType::Wet, FStatusReactionRule{
                /* bConsumeIncomingStatus */ false,
                /* bRemoveExistingStatus  */ false,
                /* BonusInstantDamage     */ 15.0f,
                /* ReactionTag            */ FName(TEXT("Conductive_Shock"))
            });

            Definitions.Add(EStatusEffectType::Electrified, Electrified);
        }

        return Definitions;
    }
};

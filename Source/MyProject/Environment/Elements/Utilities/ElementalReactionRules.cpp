#include "ElementalReactionRules.h"

const TMap<EStatusEffectType, FStatusEffectConfig>& UElementalReactionRules::GetConfigRegistry()
{
	static const TMap<EStatusEffectType, FStatusEffectConfig> Registry = []()
	{
		TMap<EStatusEffectType, FStatusEffectConfig> Definitions;

		// =====================================================================
		// 1. BURNING (Ogień)
		// =====================================================================
		{
			FStatusEffectConfig Burning;
			Burning.EffectType = EStatusEffectType::Burning;
			Burning.bIsLiquid = false;
			Burning.bRequiresFlammable = true;
			Burning.BypassTraitsIfActive = { EStatusEffectType::Oiled };
			Burning.DamagePerSecond = 5.0f;
			Burning.TickInterval = 1.0f;
			Burning.ReactionPriority = 100; // Ogień: najwyższy priorytet (anihilacja z wodą lub zapłon oleju)

			// Reakcja: Ogień trafia w Mokry cel (Steam / Extinguish)
			Burning.Reactions.Add(EStatusEffectType::Wet, FStatusReactionRule{
				/* bConsumeIncomingStatus */ true,
				/* bRemoveExistingStatus  */ true,
				/* BonusInstantDamage     */ 0.0f,
				/* ReactionTag            */ FName(TEXT("Steam_Extinguish")),
				/* ResultingStatus        */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor   */ false,
				/* ResultingDuration      */ 0.0f
			});

			// Reakcja: Ogień trafia w Naoliwiony cel (Oil Ignition / Explode)
			Burning.Reactions.Add(EStatusEffectType::Oiled, FStatusReactionRule{
				/* bConsumeIncomingStatus */ false,
				/* bRemoveExistingStatus  */ true,
				/* BonusInstantDamage     */ 25.0f,
				/* ReactionTag            */ FName(TEXT("Oil_Ignition")),
				/* ResultingStatus        */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor   */ true,
				/* ResultingDuration      */ 6.0f
			});

			Definitions.Add(EStatusEffectType::Burning, Burning);
		}

		// =====================================================================
		// 2. WET (Woda / Zmoczony)
		// =====================================================================
		{
			FStatusEffectConfig Wet;
			Wet.EffectType = EStatusEffectType::Wet;
			Wet.bIsLiquid = true; // Płyn: obmywa każdy materiał i wypiera inne płyny
			Wet.bRequiresFlammable = false;
			Wet.bRequiresConductive = false;
			Wet.DamagePerSecond = 0.0f;
			Wet.TickInterval = 1.0f;
			Wet.ReactionPriority = 80; // Woda: gasi ogień lub przewodzi prąd

			// Reakcja: Woda trafia w Płonący cel (Fire Extinguished)
			Wet.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
				/* bConsumeIncomingStatus */ true,
				/* bRemoveExistingStatus  */ true,
				/* BonusInstantDamage     */ 0.0f,
				/* ReactionTag            */ FName(TEXT("Fire_Extinguished")),
				/* ResultingStatus        */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor   */ false,
				/* ResultingDuration      */ 0.0f
			});

			// Reakcja: Woda trafia w Naelektryzowany cel (Conductive Shock)
			Wet.Reactions.Add(EStatusEffectType::Electrified, FStatusReactionRule{
				/* bConsumeIncomingStatus */ false,
				/* bRemoveExistingStatus  */ false,
				/* BonusInstantDamage     */ 15.0f,
				/* ReactionTag            */ FName(TEXT("Conductive_Shock")),
				/* ResultingStatus        */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor   */ false,
				/* ResultingDuration      */ 0.0f
			});

			Definitions.Add(EStatusEffectType::Wet, Wet);
		}

		// =====================================================================
		// 3. OILED (Olej / Naoliwiony)
		// =====================================================================
		{
			FStatusEffectConfig Oiled;
			Oiled.EffectType = EStatusEffectType::Oiled;
			Oiled.bIsLiquid = true; // Płyn: pokrywa każdy materiał i wypiera inne płyny
			Oiled.bRequiresFlammable = false;
			Oiled.bRequiresConductive = false;
			Oiled.DamagePerSecond = 0.0f;
			Oiled.TickInterval = 1.0f;
			Oiled.ReactionPriority = 60; // Olej: paliwo podtrzymujące płomień

			// Reakcja: Olej trafia w Płonący cel (Natychmiastowy zapłon oleju)
			Oiled.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
				/* bConsumeIncomingStatus */ true,
				/* bRemoveExistingStatus  */ false,
				/* BonusInstantDamage     */ 25.0f,
				/* ReactionTag            */ FName(TEXT("Oil_Ignition")),
				/* ResultingStatus        */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor   */ true,
				/* ResultingDuration      */ 6.0f
			});

			// Reakcja: Olej trafia w Naelektryzowany cel (Iskra elektryczna zapala olej)
			Oiled.Reactions.Add(EStatusEffectType::Electrified, FStatusReactionRule{
				/* bConsumeIncomingStatus */ true,
				/* bRemoveExistingStatus  */ true,
				/* BonusInstantDamage     */ 20.0f,
				/* ReactionTag            */ FName(TEXT("Oil_Electric_Ignition")),
				/* ResultingStatus        */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor   */ true,
				/* ResultingDuration      */ 6.0f
			});

			Definitions.Add(EStatusEffectType::Oiled, Oiled);
		}

		// =====================================================================
		// 4. ELECTRIFIED (Naelektryzowany)
		// =====================================================================
		{
			FStatusEffectConfig Electrified;
			Electrified.EffectType = EStatusEffectType::Electrified;
			Electrified.bIsLiquid = false;
			Electrified.bRequiresConductive = true;
			Electrified.BypassTraitsIfActive = { EStatusEffectType::Wet };
			Electrified.DamagePerSecond = 0.0f;
			Electrified.TickInterval = 1.0f;
			Electrified.ReactionPriority = 40; // Prąd: energia pasożytnicza / przewodzenie

			// Reakcja: Prąd trafia w Mokry cel (Conductive Shock)
			// Żaden status nie zostaje zniszczony - prąd i woda współistnieją, generując natychmiastowe obrażenia szokowe!
			// Prąd rozchodzi się po wodzie na sąsiednie komórki (rozprzestrzenianie jak ogień po oleju).
			Electrified.Reactions.Add(EStatusEffectType::Wet, FStatusReactionRule{
				/* bConsumeIncomingStatus */ false,
				/* bRemoveExistingStatus  */ false,
				/* BonusInstantDamage     */ 15.0f,
				/* ReactionTag            */ FName(TEXT("Conductive_Shock")),
				/* ResultingStatus        */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor   */ true,
				/* ResultingDuration      */ 4.0f
			});

			// Reakcja: Prąd trafia w Naoliwiony cel (Iskra elektryczna zapala olej)
			// Iskra elektryczna detonuje łatwopalny olej, wywołując zapłon (Burning) i natychmiastowe obrażenia
			Electrified.Reactions.Add(EStatusEffectType::Oiled, FStatusReactionRule{
				/* bConsumeIncomingStatus */ true,
				/* bRemoveExistingStatus  */ true,
				/* BonusInstantDamage     */ 20.0f,
				/* ReactionTag            */ FName(TEXT("Oil_Electric_Ignition")),
				/* ResultingStatus        */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor   */ true,
				/* ResultingDuration      */ 6.0f
			});

			Definitions.Add(EStatusEffectType::Electrified, Electrified);
		}

		return Definitions;
	}();

	return Registry;
}

const FStatusEffectConfig& UElementalReactionRules::GetEffectConfig(EStatusEffectType Status)
{
	const TMap<EStatusEffectType, FStatusEffectConfig>& Registry = GetConfigRegistry();
	if (const FStatusEffectConfig* Found = Registry.Find(Status))
	{
		return *Found;
	}

	static const FStatusEffectConfig EmptyConfig;
	return EmptyConfig;
}

bool UElementalReactionRules::IsLiquidStatus(EStatusEffectType Status)
{
	return GetEffectConfig(Status).bIsLiquid;
}

bool UElementalReactionRules::CanMaterialReceiveStatus(
	EPhysicalMaterialType Material,
	EStatusEffectType IncomingStatus,
	const TArray<EStatusEffectType>& ActiveStatuses)
{
	const FStatusEffectConfig& Config = GetEffectConfig(IncomingStatus);

	// 1. Płyny oraz statusy bez restrykcji materiałowych mogą pokryć dowolny materiał
	if (Config.bIsLiquid || (!Config.bRequiresFlammable && !Config.bRequiresConductive))
	{
		return true;
	}

	// 2. Statusy omijające restrykcję materiałową (np. Oiled pozwala podpalić kamień/metal, Wet przewodzi prąd)
	for (EStatusEffectType BypassStatus : Config.BypassTraitsIfActive)
	{
		if (ActiveStatuses.Contains(BypassStatus))
		{
			return true;
		}
	}

	// 3. Weryfikacja przez cechy fizyczne tożsamości materiałowej (Traits)
	const FPhysicalMaterialTraits Traits = PhysicalMaterialUtils::GetTraits(Material);

	if (Config.bRequiresFlammable && !Traits.bFlammable)
	{
		return false;
	}

	if (Config.bRequiresConductive && !Traits.bConductive)
	{
		return false;
	}

	return true;
}

FElementalReactionResult UElementalReactionRules::EvaluateReaction(
	EStatusEffectType IncomingStatus,
	const TArray<EStatusEffectType>& ActiveStatuses)
{
	FElementalReactionResult Result;
	const FStatusEffectConfig& IncomingConfig = GetEffectConfig(IncomingStatus);

	// =========================================================================
	// FAZA 1: Dedykowane reguły reakcji zdefiniowane w karcie przychodzącego żywiołu
	// =========================================================================
	for (EStatusEffectType ActiveStatus : ActiveStatuses)
	{
		if (const FStatusReactionRule* Rule = IncomingConfig.Reactions.Find(ActiveStatus))
		{
			Result.bReactionOccurred = true;
			Result.bConsumeIncomingStatus = Rule->bConsumeIncomingStatus;
			Result.ExistingStatusToRemove = Rule->bRemoveExistingStatus ? ActiveStatus : EStatusEffectType::None;

			if (Rule->ResultingStatus != EStatusEffectType::None)
			{
				Result.ResultingStatus = Rule->ResultingStatus;
			}
			else if (Rule->bConsumeIncomingStatus)
			{
				// Przychodzący status uległ zużyciu/odparowaniu/zgaszeniu - nie pozostaje na celu
				Result.ResultingStatus = EStatusEffectType::None;
			}
			else if (Rule->bRemoveExistingStatus)
			{
				// Dotychczasowy status został usunięty, a przychodzący nie został zużyty (przejmuje cel)
				Result.ResultingStatus = IncomingStatus;
			}
			else
			{
				// Żaden status nie został usunięty ani zamieniony (współistnienie, np. prąd w wodzie)
				Result.ResultingStatus = EStatusEffectType::None;
			}

			Result.bCanSpreadToNeighbor = Rule->bCanSpreadToNeighbor;
			Result.ResultingDuration = Rule->ResultingDuration;
			Result.BonusInstantDamage = Rule->BonusInstantDamage;
			Result.ReactionTag = Rule->ReactionTag;
			return Result;
		}
	}

	// =========================================================================
	// FAZA 2: Reguła Powłok Płynnych (Liquid Displacement / Mutual Exclusivity)
	// Każdy nowy płyn wypiera poprzednio nałożony płyn (np. Wet wypiera Oiled, Oiled wypiera Wet).
	// =========================================================================
	if (IncomingConfig.bIsLiquid)
	{
		for (EStatusEffectType ActiveStatus : ActiveStatuses)
		{
			if (ActiveStatus != IncomingStatus && IsLiquidStatus(ActiveStatus))
			{
				Result.bReactionOccurred = true;
				Result.bConsumeIncomingStatus = false; // Nowy płyn nakłada się na cel
				Result.ExistingStatusToRemove = ActiveStatus; // Poprzedni płyn zostaje wyparty/zmyty
				Result.ResultingStatus = IncomingStatus;
				Result.bCanSpreadToNeighbor = false;
				Result.ResultingDuration = 0.0f;
				Result.BonusInstantDamage = 0.0f;
				Result.ReactionTag = FName(TEXT("Liquid_Displaced"));
				return Result;
			}
		}
	}

	return Result;
}

bool UElementalReactionRules::CanSpreadToNeighbor(
	EStatusEffectType SourceStatus,
	EStatusEffectType TargetStatus,
	FElementalReactionResult& OutReactionResult)
{
	OutReactionResult = FElementalReactionResult();

	if (SourceStatus == EStatusEffectType::None || TargetStatus == EStatusEffectType::None || SourceStatus == TargetStatus)
	{
		return false;
	}

	OutReactionResult = EvaluateReaction(SourceStatus, { TargetStatus });
	return OutReactionResult.bReactionOccurred && OutReactionResult.bCanSpreadToNeighbor;
}

int32 UElementalReactionRules::GetReactionPriority(EStatusEffectType Status)
{
	return GetEffectConfig(Status).ReactionPriority;
}

void UElementalReactionRules::SortByReactionPriority(TArray<EStatusEffectType>& InOutStatuses)
{
	InOutStatuses.Sort([](EStatusEffectType A, EStatusEffectType B)
	{
		return GetReactionPriority(A) > GetReactionPriority(B);
	});
}


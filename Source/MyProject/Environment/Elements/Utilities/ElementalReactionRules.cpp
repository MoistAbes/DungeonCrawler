#include "ElementalReactionRules.h"
#include "MyProject/Environment/Elements/Data/DungeonElementalSettings.h"
#include "MyProject/Environment/Elements/Data/StatusEffectConfigAsset.h"
#include "MyProject/Logging/DungeonLogCategories.h"

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
			Burning.bIsDoTType = true;
			Burning.bIsLiquid = false;
			Burning.bRequiresFlammable = true;
			Burning.BypassTraitsIfActive = { EStatusEffectType::Oiled };
			Burning.ReactionPriority = 100; // Ogień: najwyższy priorytet (anihilacja z wodą lub zapłon oleju)

			// Tiery ognia: Tier 0 = bazowy (5 dps / 5s), Tier 1 = silny (10 dps / 6s), Tier 2 = piekielny (15 dps / 8s)
			Burning.Tiers = {
				{ /* DamagePerSecond */ 5.0f,  /* BaseDuration */ 5.0f, /* TickInterval */ 1.0f },
				{ /* DamagePerSecond */ 10.0f, /* BaseDuration */ 6.0f, /* TickInterval */ 1.0f },
				{ /* DamagePerSecond */ 15.0f, /* BaseDuration */ 8.0f, /* TickInterval */ 0.5f }
			};

			// Reakcja: Ogień trafia w Mokry cel (Steam / Extinguish)
			Burning.Reactions.Add(EStatusEffectType::Wet, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ true,
				/* bRemoveExistingStatus   */ true,
				/* ReactionTag             */ FName(TEXT("Steam_Extinguish")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ false,
				/* ResultingDuration       */ 0.0f,
				/* bSyncWithCarrierDuration */ false
			});

			// Reakcja: Ogień trafia w Naoliwiony cel (Zapłon plamy oleju / Oil Ignition)
			// Olej nie jest natychmiast niszczony, lecz staje się paliwem podtrzymującym płomień [Oiled, Burning]
			Burning.Reactions.Add(EStatusEffectType::Oiled, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ false,
				/* ReactionTag             */ FName(TEXT("Oil_Ignition")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ true,
				/* ResultingDuration       */ 6.0f,
				/* bSyncWithCarrierDuration */ true
			});

			Definitions.Add(EStatusEffectType::Burning, Burning);
		}

		// =====================================================================
		// 2. WET (Woda / Zmoczony)
		// =====================================================================
		{
			FStatusEffectConfig Wet;
			Wet.EffectType = EStatusEffectType::Wet;
			Wet.bIsDoTType = false;
			Wet.bIsLiquid = true; // Płyn: obmywa każdy materiał i wypiera inne płyny
			Wet.bRequiresFlammable = false;
			Wet.bRequiresConductive = false;
			Wet.ReactionPriority = 80; // Woda: gasi ogień lub przewodzi prąd

			// Tier 0 dla wody: brak DoT, trwa bazowo 6s
			Wet.Tiers = {
				{ /* DamagePerSecond */ 0.0f, /* BaseDuration */ 6.0f, /* TickInterval */ 1.0f }
			};

			// Reakcja: Woda trafia w Płonący cel (Fire Extinguished)
			Wet.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ true,
				/* bRemoveExistingStatus   */ true,
				/* ReactionTag             */ FName(TEXT("Fire_Extinguished")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ false,
				/* ResultingDuration       */ 0.0f,
				/* bSyncWithCarrierDuration */ false
			});

			// Reakcja: Woda trafia w Naelektryzowany cel (Conductive Shock)
			// Woda jest nośnikiem płynnym - NIE synchronizuje swojego czasu z pasożytniczym prądem
			Wet.Reactions.Add(EStatusEffectType::Electrified, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ false,
				/* ReactionTag             */ FName(TEXT("Conductive_Shock")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ false,
				/* ResultingDuration       */ 4.0f,
				/* bSyncWithCarrierDuration */ false
			});

			Definitions.Add(EStatusEffectType::Wet, Wet);
		}

		// =====================================================================
		// 3. OILED (Olej / Naoliwiony)
		// =====================================================================
		{
			FStatusEffectConfig Oiled;
			Oiled.EffectType = EStatusEffectType::Oiled;
			Oiled.bIsDoTType = false;
			Oiled.bIsLiquid = true; // Płyn: pokrywa każdy materiał i wypiera inne płyny
			Oiled.bRequiresFlammable = false;
			Oiled.bRequiresConductive = false;
			Oiled.ReactionPriority = 60; // Olej: paliwo podtrzymujące płomień

			// Tier 0 dla oleju: brak DoT, trwa bazowo 6s
			Oiled.Tiers = {
				{ /* DamagePerSecond */ 0.0f, /* BaseDuration */ 6.0f, /* TickInterval */ 1.0f }
			};

			// Reakcja: Olej trafia w Płonący cel (Natychmiastowy zapłon nowo wylanego oleju)
			// Olej jest paliwem - NIE synchronizuje swojego czasu trwania z płomieniem i sam nie rozprzestrzenia się na sąsiadów
			Oiled.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ false,
				/* ReactionTag             */ FName(TEXT("Oil_Ignition")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ false,
				/* ResultingDuration       */ 6.0f,
				/* bSyncWithCarrierDuration */ false
			});

			// Reakcja: Olej trafia w Naelektryzowany cel (Iskra elektryczna zapala olej)
			Oiled.Reactions.Add(EStatusEffectType::Electrified, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ true,
				/* ReactionTag             */ FName(TEXT("Oil_Electric_Ignition")),
				/* ResultingStatus         */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor    */ false,
				/* ResultingDuration       */ 6.0f,
				/* bSyncWithCarrierDuration */ false
			});

			Definitions.Add(EStatusEffectType::Oiled, Oiled);
		}

		// =====================================================================
		// 4. ELECTRIFIED (Naelektryzowany)
		// =====================================================================
		{
			FStatusEffectConfig Electrified;
			Electrified.EffectType = EStatusEffectType::Electrified;
			Electrified.bIsDoTType = true;
			Electrified.bIsLiquid = false;
			Electrified.bRequiresConductive = true;
			Electrified.BypassTraitsIfActive = { EStatusEffectType::Wet };
			Electrified.ReactionPriority = 40; // Prąd: energia pasożytnicza / przewodzenie

			// Tiery prądu: Tier 0 = szok elektryczny (4 dps / 4s), Tier 1 = wyładowanie łukowe (8 dps / 5s)
			Electrified.Tiers = {
				{ /* DamagePerSecond */ 4.0f, /* BaseDuration */ 4.0f, /* TickInterval */ 1.0f },
				{ /* DamagePerSecond */ 8.0f, /* BaseDuration */ 5.0f, /* TickInterval */ 0.5f }
			};

			// Reakcja: Prąd trafia w Mokry cel (Conductive Shock)
			// bSyncWithCarrierDuration = true: prąd elektryzuje całą kałużę i trwa tak długo jak woda!
			Electrified.Reactions.Add(EStatusEffectType::Wet, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ false,
				/* ReactionTag             */ FName(TEXT("Conductive_Shock")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ true,
				/* ResultingDuration       */ 4.0f,
				/* bSyncWithCarrierDuration */ true
			});

			// Reakcja: Prąd trafia w Naoliwiony cel (Iskra elektryczna zapala olej)
			Electrified.Reactions.Add(EStatusEffectType::Oiled, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ true,
				/* bRemoveExistingStatus   */ false,
				/* ReactionTag             */ FName(TEXT("Oil_Electric_Ignition")),
				/* ResultingStatus         */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor    */ true,
				/* ResultingDuration       */ 6.0f,
				/* bSyncWithCarrierDuration */ true
			});

			Definitions.Add(EStatusEffectType::Electrified, Electrified);
		}

		return Definitions;
	}();

	return Registry;
}

const FStatusEffectConfig& UElementalReactionRules::GetEffectConfig(EStatusEffectType Status)
{
	// 1. Sprawdzamy czy w ustawieniach projektu skonfigurowano UStatusEffectConfigAsset (z buforowaniem)
	if (const UDungeonElementalSettings* Settings = GetDefault<UDungeonElementalSettings>())
	{
		static TWeakObjectPtr<UStatusEffectConfigAsset> CachedAsset = nullptr;
		if (!CachedAsset.IsValid())
		{
			CachedAsset = Settings->StatusEffectConfigAsset.LoadSynchronous();
		}

		if (UStatusEffectConfigAsset* ConfigAsset = CachedAsset.Get())
		{
			if (const FStatusEffectConfig* FoundInAsset = ConfigAsset->FindConfig(Status))
			{
				return *FoundInAsset;
			}
		}
	}

	// 2. Bezpieczny fallback C++ (zbudowany na starcie, zero crashy)
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static TSet<EStatusEffectType> WarnedStatuses;
	if (!WarnedStatuses.Contains(Status))
	{
		WarnedStatuses.Add(Status);
		UE_LOG(LogDungeonElements, Warning,
			TEXT("[Config] GetEffectConfig(%s): DataAsset missing or incomplete, using C++ fallback. "
			     "This may cause divergence from designer-tuned values."),
			*UEnum::GetValueAsString(Status));
	}
#endif

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

			// ZŁOTA ZASADA CHEMICZNA: Jeśli przychodzący płyn nie uległ zgaszeniu/zużyciu i cel ma inny płyn,
			// a dedykowana reguła nie wskazała innego statusu do usunięcia, to istniejący płyn zostaje bezwzględnie wyparty!
			if (IncomingConfig.bIsLiquid && !Result.bConsumeIncomingStatus && Result.ExistingStatusToRemove == EStatusEffectType::None)
			{
				for (EStatusEffectType OtherActive : ActiveStatuses)
				{
					if (OtherActive != IncomingStatus && IsLiquidStatus(OtherActive))
					{
						Result.ExistingStatusToRemove = OtherActive;
						break;
					}
				}
			}

			Result.bCanSpreadToNeighbor = Rule->bCanSpreadToNeighbor;
			Result.ResultingDuration = Rule->ResultingDuration;
			Result.bSyncWithCarrierDuration = Rule->bSyncWithCarrierDuration;
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

bool UElementalReactionRules::DoesStatusSyncWithCarrier(EStatusEffectType DependentStatus, EStatusEffectType CarrierStatus)
{
	if (DependentStatus == EStatusEffectType::None || CarrierStatus == EStatusEffectType::None || DependentStatus == CarrierStatus)
	{
		return false;
	}

	const FStatusEffectConfig& Config = GetEffectConfig(DependentStatus);
	if (const FStatusReactionRule* Rule = Config.Reactions.Find(CarrierStatus))
	{
		return Rule->bSyncWithCarrierDuration;
	}

	return false;
}

bool UElementalReactionRules::RequiresCarrierToSustain(EPhysicalMaterialType Material, EStatusEffectType Status)
{
	return !CanMaterialReceiveStatus(Material, Status, {});
}


float UElementalReactionRules::ApplyCarrierSync(
	EPhysicalMaterialType Material,
	EStatusEffectType Status,
	float CurrentDuration,
	EStatusEffectType CarrierStatus,
	float CarrierRemainingTime,
	bool bSyncWithCarrier)
{
	if (CarrierStatus == Status || CarrierRemainingTime <= 0.0f)
	{
		return CurrentDuration;
	}

	if (!DoesStatusSyncWithCarrier(Status, CarrierStatus) &&
		!GetEffectConfig(Status).BypassTraitsIfActive.Contains(CarrierStatus))
	{
		return CurrentDuration;
	}

	if (RequiresCarrierToSustain(Material, Status))
	{
		if (bSyncWithCarrier || DoesStatusSyncWithCarrier(Status, CarrierStatus))
		{
			return CarrierRemainingTime;
		}
		else
		{
			return FMath::Min(CurrentDuration, CarrierRemainingTime);
		}
	}
	else if (bSyncWithCarrier || DoesStatusSyncWithCarrier(Status, CarrierStatus))
	{
		return FMath::Max(CurrentDuration, CarrierRemainingTime);
	}

	return CurrentDuration;
}

bool UElementalReactionRules::IsPermanentStatus(EPhysicalMaterialType Material, EStatusEffectType Status)
{
	return (Status == EStatusEffectType::Burning) && PhysicalMaterialUtils::GetTraits(Material).bSelfSustainingFuel;
}

FElementalTransitionPlan UElementalReactionRules::CalculateElementalTransition(
	EPhysicalMaterialType TargetMaterial,
	EStatusEffectType IncomingStatus,
	float IncomingDuration,
	uint8 IncomingTier,
	const TArray<FElementalActiveStatusSnapshot>& CurrentStatuses)
{
	FElementalTransitionPlan Plan;

	if (IncomingStatus == EStatusEffectType::None || IncomingDuration < 0.0f)
	{
		return Plan;
	}

	struct FWorkingStatusEntry
	{
		EStatusEffectType Status = EStatusEffectType::None;
		uint8 Tier = 0;
		float Duration = 0.0f;
		bool bPermanent = false;
		bool bSyncWithCarrier = false;
		bool bWasOriginallyPresent = false;
	};

	TArray<FWorkingStatusEntry> WorkingEntries;
	TArray<EStatusEffectType> OriginalStatusTypes;
	for (const FElementalActiveStatusSnapshot& Snap : CurrentStatuses)
	{
		if (Snap.Status != EStatusEffectType::None)
		{
			WorkingEntries.Add({ Snap.Status, Snap.Tier, Snap.RemainingDuration, Snap.bPermanent, false, true });
			OriginalStatusTypes.AddUnique(Snap.Status);
		}
	}

	auto GetWorkingTypes = [&]() -> TArray<EStatusEffectType>
	{
		TArray<EStatusEffectType> Types;
		for (const auto& E : WorkingEntries)
		{
			Types.Add(E.Status);
		}
		return Types;
	};

	auto FindWorkingEntry = [&](EStatusEffectType S) -> FWorkingStatusEntry*
	{
		return WorkingEntries.FindByPredicate([S](const FWorkingStatusEntry& E) { return E.Status == S; });
	};

	auto ComputeDuration = [&](EStatusEffectType Status, float ReqDuration, bool bSyncCarrier) -> float
	{
		if (IsPermanentStatus(TargetMaterial, Status))
		{
			return 0.0f;
		}

		const float FallbackDuration = GetEffectConfig(Status).GetBaseDuration(IncomingTier);
		float FinalDuration = (ReqDuration > 0.0f) ? ReqDuration : FallbackDuration;

		for (const FWorkingStatusEntry& Other : WorkingEntries)
		{
			if (Other.Status != Status)
			{
				const float CarrierRemaining = Other.bPermanent ? FallbackDuration : Other.Duration;
				FinalDuration = ApplyCarrierSync(
					TargetMaterial,
					Status,
					FinalDuration,
					Other.Status,
					CarrierRemaining,
					bSyncCarrier);
			}
		}

		return FMath::Max(0.1f, FinalDuration);
	};

	// 1. Pusty cel: sprawdzamy czy materiał może przyjąć ten status
	if (WorkingEntries.IsEmpty())
	{
		if (!CanMaterialReceiveStatus(TargetMaterial, IncomingStatus, {}))
		{
			Plan.bAccepted = false;
			return Plan;
		}

		const float FinalDuration = ComputeDuration(IncomingStatus, IncomingDuration, false);
		const bool bPerm = IsPermanentStatus(TargetMaterial, IncomingStatus);

		Plan.bAccepted = true;
		Plan.bStateModified = true;
		Plan.StatusesToApply.Add({ IncomingStatus, IncomingTier, FinalDuration, bPerm, false });
		return Plan;
	}

	// 2. Identyczny żywioł: odświeżamy czas trwania i synchronizujemy nośniki
	if (FWorkingStatusEntry* Existing = FindWorkingEntry(IncomingStatus))
	{
		Existing->Tier = FMath::Max(Existing->Tier, IncomingTier);
		const float NewDuration = ComputeDuration(IncomingStatus, IncomingDuration, false);
		Existing->Duration = FMath::Max(Existing->Duration, NewDuration);

		Plan.bAccepted = true;
		Plan.bStateModified = true;
		Plan.StatusesToApply.Add({ Existing->Status, Existing->Tier, Existing->Duration, Existing->bPermanent, false });

		// Synchronizacja nośników
		for (FWorkingStatusEntry& Other : WorkingEntries)
		{
			if (Other.Status != IncomingStatus && DoesStatusSyncWithCarrier(Other.Status, IncomingStatus))
			{
				Other.Duration = ApplyCarrierSync(TargetMaterial, Other.Status, Other.Duration, IncomingStatus, Existing->Duration, true);
				Plan.StatusesToApply.Add({ Other.Status, Other.Tier, Other.Duration, Other.bPermanent, true });
			}
		}

		return Plan;
	}

	// 3. Różny żywioł: KROK 1 - Reakcja pierwotna (Ingress Reaction)
	TArray<EStatusEffectType> ActiveTypesBefore = GetWorkingTypes();
	SortByReactionPriority(ActiveTypesBefore);
	const FElementalReactionResult Reaction = EvaluateReaction(IncomingStatus, ActiveTypesBefore);
	Plan.PrimaryReaction = Reaction;

	if (Reaction.bReactionOccurred)
	{
		if (Reaction.ReactionTag != NAME_None)
		{
			Plan.TriggeredReactionTags.AddUnique(Reaction.ReactionTag);
		}

		// A. Usunięcie wygaszonego / skonsumowanego statusu
		if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
		{
			WorkingEntries.RemoveAll([&](const FWorkingStatusEntry& E) { return E.Status == Reaction.ExistingStatusToRemove; });
		}

		// B. Dodanie przychodzącego statusu (jeśli nie został zużyty, np. Oiled)
		if (!Reaction.bConsumeIncomingStatus)
		{
			if (CanMaterialReceiveStatus(TargetMaterial, IncomingStatus, GetWorkingTypes()))
			{
				const float FinalDuration = ComputeDuration(IncomingStatus, IncomingDuration, Reaction.bSyncWithCarrierDuration);
				const bool bPerm = IsPermanentStatus(TargetMaterial, IncomingStatus);
				WorkingEntries.Add({ IncomingStatus, IncomingTier, FinalDuration, bPerm, Reaction.bSyncWithCarrierDuration, false });
			}
		}

		// C. Dodanie statusu wynikowego reakcji (jeśli powstał, np. Burning z iskry na oleju)
		if (Reaction.ResultingStatus != EStatusEffectType::None)
		{
			if (CanMaterialReceiveStatus(TargetMaterial, Reaction.ResultingStatus, GetWorkingTypes()))
			{
				const float ReqDur = (Reaction.ResultingDuration > 0.0f) ? Reaction.ResultingDuration : GetEffectConfig(Reaction.ResultingStatus).GetBaseDuration(IncomingTier);
				const float FinalDuration = ComputeDuration(Reaction.ResultingStatus, ReqDur, Reaction.bSyncWithCarrierDuration);
				const bool bPerm = IsPermanentStatus(TargetMaterial, Reaction.ResultingStatus);
				WorkingEntries.Add({ Reaction.ResultingStatus, IncomingTier, FinalDuration, bPerm, Reaction.bSyncWithCarrierDuration, false });
			}
		}
	}
	else
	{
		// Brak reakcji bezpośredniej: sprawdzamy czy materiał i obecne powłoki pozwalają na koegzystencję
		if (!CanMaterialReceiveStatus(TargetMaterial, IncomingStatus, GetWorkingTypes()))
		{
			Plan.bAccepted = false;
			return Plan;
		}

		const float FinalDuration = ComputeDuration(IncomingStatus, IncomingDuration, false);
		const bool bPerm = IsPermanentStatus(TargetMaterial, IncomingStatus);
		WorkingEntries.Add({ IncomingStatus, IncomingTier, FinalDuration, bPerm, false, false });
	}

	// 4. KROK 2 - Pętla Równowagi Chemicznej (Chemical Equilibrium Loop)
	// Rozwiązuje reakcje wewnętrzne pomiędzy współistniejącymi statusami (np. Burning vs Wet -> Steam_Extinguish)
	int32 Iteration = 0;
	bool bChanged = true;
	while (bChanged && Iteration++ < 3 && WorkingEntries.Num() > 1)
	{
		bChanged = false;
		TArray<EStatusEffectType> CurrentTypes = GetWorkingTypes();
		SortByReactionPriority(CurrentTypes);

		for (int32 i = 0; i < CurrentTypes.Num() && !bChanged; ++i)
		{
			for (int32 j = i + 1; j < CurrentTypes.Num() && !bChanged; ++j)
			{
				const FElementalReactionResult SubReaction = EvaluateReaction(CurrentTypes[i], { CurrentTypes[j] });
				if (SubReaction.bReactionOccurred &&
					(SubReaction.ExistingStatusToRemove != EStatusEffectType::None ||
					 SubReaction.bConsumeIncomingStatus ||
					 SubReaction.ResultingStatus != EStatusEffectType::None))
				{
					if (SubReaction.ReactionTag != NAME_None)
					{
						Plan.TriggeredReactionTags.AddUnique(SubReaction.ReactionTag);
					}

					if (SubReaction.ExistingStatusToRemove != EStatusEffectType::None)
					{
						WorkingEntries.RemoveAll([&](const FWorkingStatusEntry& E) { return E.Status == SubReaction.ExistingStatusToRemove; });
					}
					if (SubReaction.bConsumeIncomingStatus)
					{
						WorkingEntries.RemoveAll([&](const FWorkingStatusEntry& E) { return E.Status == CurrentTypes[i]; });
					}
					if (SubReaction.ResultingStatus != EStatusEffectType::None)
					{
						if (CanMaterialReceiveStatus(TargetMaterial, SubReaction.ResultingStatus, GetWorkingTypes()))
						{
							const float ReqDur = (SubReaction.ResultingDuration > 0.0f) ? SubReaction.ResultingDuration : GetEffectConfig(SubReaction.ResultingStatus).GetBaseDuration();
							const float FinalDuration = ComputeDuration(SubReaction.ResultingStatus, ReqDur, SubReaction.bSyncWithCarrierDuration);
							const bool bPerm = IsPermanentStatus(TargetMaterial, SubReaction.ResultingStatus);
							WorkingEntries.Add({ SubReaction.ResultingStatus, 0, FinalDuration, bPerm, SubReaction.bSyncWithCarrierDuration, false });
						}
					}

					bChanged = true;
				}
			}
		}
	}

	// 5. KROK 3 - Złota Zasada Płynów (Liquid Mutual Exclusivity)
	// Tylko jeden płyn może pokrywać daną powierzchnię w tym samym czasie.
	// Nowszy płyn (IncomingStatus) lub płyn o wyższym priorytecie wypiera pozostałe.
	EStatusEffectType DominantLiquid = EStatusEffectType::None;
	if (IsLiquidStatus(IncomingStatus) && FindWorkingEntry(IncomingStatus))
	{
		DominantLiquid = IncomingStatus;
	}
	else
	{
		for (const FWorkingStatusEntry& Entry : WorkingEntries)
		{
			if (IsLiquidStatus(Entry.Status))
			{
				if (DominantLiquid == EStatusEffectType::None || GetReactionPriority(Entry.Status) > GetReactionPriority(DominantLiquid))
				{
					DominantLiquid = Entry.Status;
				}
			}
		}
	}

	if (DominantLiquid != EStatusEffectType::None)
	{
		WorkingEntries.RemoveAll([&](const FWorkingStatusEntry& E) {
			return E.Status != DominantLiquid && IsLiquidStatus(E.Status);
		});
	}

	// 6. KROK 4 - Czyszczenie osieroconych statusów (Clean Orphaned Statuses)
	for (int32 Index = WorkingEntries.Num() - 1; Index >= 0; --Index)
	{
		const EStatusEffectType StatusToCheck = WorkingEntries[Index].Status;
		TArray<EStatusEffectType> OtherActive;
		for (int32 OtherIdx = 0; OtherIdx < WorkingEntries.Num(); ++OtherIdx)
		{
			if (OtherIdx != Index)
			{
				OtherActive.Add(WorkingEntries[OtherIdx].Status);
			}
		}

		if (!CanMaterialReceiveStatus(TargetMaterial, StatusToCheck, OtherActive))
		{
			WorkingEntries.RemoveAt(Index);
		}
	}

	// 7. KROK 5 - Budowa wynikowego planu przejścia (Final Plan Assembly)
	Plan.bAccepted = true;
	TArray<EStatusEffectType> FinalActiveTypes = GetWorkingTypes();

	for (EStatusEffectType OrigStatus : OriginalStatusTypes)
	{
		if (!FinalActiveTypes.Contains(OrigStatus))
		{
			Plan.StatusesToRemove.Add(OrigStatus);
			Plan.bStateModified = true;
		}
	}

	for (const FWorkingStatusEntry& Entry : WorkingEntries)
	{
		Plan.StatusesToApply.Add({ Entry.Status, Entry.Tier, Entry.Duration, Entry.bPermanent, Entry.bSyncWithCarrier });
		if (!OriginalStatusTypes.Contains(Entry.Status))
		{
			Plan.bStateModified = true;
		}
	}

	Plan.bBecameEmpty = WorkingEntries.IsEmpty();
	if (Plan.StatusesToRemove.Num() > 0 || Plan.StatusesToApply.Num() > 0)
	{
		Plan.bStateModified = true;
	}

	return Plan;
}


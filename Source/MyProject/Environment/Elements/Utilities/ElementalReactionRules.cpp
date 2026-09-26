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
			// Olej jest paliwem - NIE synchronizuje swojego czasu trwania z płomieniem
			Oiled.Reactions.Add(EStatusEffectType::Burning, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ false,
				/* ReactionTag             */ FName(TEXT("Oil_Ignition")),
				/* ResultingStatus         */ EStatusEffectType::None,
				/* bCanSpreadToNeighbor    */ true,
				/* ResultingDuration       */ 6.0f,
				/* bSyncWithCarrierDuration */ false
			});

			// Reakcja: Olej trafia w Naelektryzowany cel (Iskra elektryczna zapala olej)
			Oiled.Reactions.Add(EStatusEffectType::Electrified, FStatusReactionRule{
				/* bConsumeIncomingStatus  */ false,
				/* bRemoveExistingStatus   */ true,
				/* ReactionTag             */ FName(TEXT("Oil_Electric_Ignition")),
				/* ResultingStatus         */ EStatusEffectType::Burning,
				/* bCanSpreadToNeighbor    */ true,
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
	// 1. Sprawdzamy czy w ustawieniach projektu skonfigurowano UStatusEffectConfigAsset
	if (const UDungeonElementalSettings* Settings = GetDefault<UDungeonElementalSettings>())
	{
		if (UStatusEffectConfigAsset* ConfigAsset = Settings->StatusEffectConfigAsset.LoadSynchronous())
		{
			if (const FStatusEffectConfig* FoundInAsset = ConfigAsset->FindConfig(Status))
			{
				return *FoundInAsset;
			}
		}
	}

	// 2. Bezpieczny fallback C++ (zbudowany na starcie, zero crashy)
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

bool UElementalReactionRules::CleanOrphanedStatuses(FSurfaceCellData& InOutCellData, EStatusEffectType StatusToPreserve)
{
	bool bEvictedAny = false;
	const TArray<EStatusEffectType> ActiveBefore = InOutCellData.GetStatusTypes();
	for (int32 Index = InOutCellData.ActiveStatuses.Num() - 1; Index >= 0; --Index)
	{
		const EStatusEffectType RemainingStatus = InOutCellData.ActiveStatuses[Index].Status;
		if (RemainingStatus == StatusToPreserve)
		{
			continue;
		}

		if (!CanMaterialReceiveStatus(InOutCellData.SurfaceMaterial, RemainingStatus, ActiveBefore))
		{
			UE_LOG(LogDungeonElements, Log, TEXT("[ElementalReactionRules] CleanOrphanedStatuses: Evicted orphaned status %s on material %s (carrier expired)"),
				*UEnum::GetValueAsString(RemainingStatus), *UEnum::GetValueAsString(InOutCellData.SurfaceMaterial));
			InOutCellData.ActiveStatuses.RemoveAt(Index);
			bEvictedAny = true;
		}
	}
	return bEvictedAny;
}

FSurfaceCellTransitionResult UElementalReactionRules::CalculateCellTransition(
	FSurfaceCellData& InOutCellData,
	EStatusEffectType IncomingStatus,
	float Duration,
	AActor* Instigator,
	float CurrentTime)
{
	FSurfaceCellTransitionResult Result;

	if (IncomingStatus == EStatusEffectType::None || Duration <= 0.0f)
	{
		return Result;
	}

	// 1. Pusta komórka: sprawdzamy czy materiał może przyjąć ten status
	if (InOutCellData.IsEmpty())
	{
		if (!CanMaterialReceiveStatus(InOutCellData.SurfaceMaterial, IncomingStatus, {}))
		{
			Result.bAccepted = false;
			return Result;
		}

		InOutCellData.ActiveStatuses.Add({ IncomingStatus, CurrentTime + Duration, Instigator });
		Result.bAccepted = true;
		Result.bStateModified = true;
		return Result;
	}

	// 2. Identyczny żywioł: odświeżamy czas trwania i synchronizujemy nośniki
	if (FSurfaceCellStatusEntry* ExistingEntry = InOutCellData.FindStatus(IncomingStatus))
	{
		float AllowedDuration = Duration;
		// Jeśli odświeżany status zależy od innego nośnika (np. prąd na wodzie, ogień na oleju),
		// jego czas trwania NIE MOŻE przekroczyć pozostałego czasu trwania tego nośnika!
		for (const FSurfaceCellStatusEntry& Entry : InOutCellData.ActiveStatuses)
		{
			if (Entry.Status != IncomingStatus && DoesStatusSyncWithCarrier(IncomingStatus, Entry.Status))
			{
				const float CarrierRemaining = Entry.ServerEndTime - CurrentTime;
				if (CarrierRemaining > 0.0f)
				{
					AllowedDuration = FMath::Min(AllowedDuration, CarrierRemaining);
				}
			}
		}

		ExistingEntry->ServerEndTime = FMath::Max(ExistingEntry->ServerEndTime, CurrentTime + AllowedDuration);
		ExistingEntry->Instigator = Instigator;

		// Sprawdzamy, czy w komórce współistnieją inne statusy zależne od tego nośnika (np. prąd na wodzie przy dolewaniu wody)
		for (FSurfaceCellStatusEntry& OtherEntry : InOutCellData.ActiveStatuses)
		{
			if (OtherEntry.Status != IncomingStatus && DoesStatusSyncWithCarrier(OtherEntry.Status, IncomingStatus))
			{
				OtherEntry.ServerEndTime = FMath::Max(OtherEntry.ServerEndTime, ExistingEntry->ServerEndTime);
			}
		}

		Result.bAccepted = true;
		Result.bStateModified = true;
		return Result;
	}

	// 3. Różny żywioł: ewaluacja reakcji chemicznej
	TArray<EStatusEffectType> ActiveStatusesBefore = InOutCellData.GetStatusTypes();
	SortByReactionPriority(ActiveStatusesBefore);
	const FElementalReactionResult Reaction = EvaluateReaction(IncomingStatus, ActiveStatusesBefore);
	Result.Reaction = Reaction;

	if (Reaction.bReactionOccurred)
	{
		// A. Status wynikowy reakcji (np. Olej + Iskra -> Burning)
		if (Reaction.ResultingStatus != EStatusEffectType::None)
		{
			if (CanMaterialReceiveStatus(InOutCellData.SurfaceMaterial, Reaction.ResultingStatus, ActiveStatusesBefore))
			{
				const float FallbackResultDuration = (Duration > 0.0f) ? Duration : GetEffectConfig(Reaction.ResultingStatus).GetBaseDuration();
				float NewDuration = (Reaction.ResultingDuration > 0.0f) ? Reaction.ResultingDuration : FallbackResultDuration;
				if (Reaction.bSyncWithCarrierDuration)
				{
					for (const FSurfaceCellStatusEntry& Entry : InOutCellData.ActiveStatuses)
					{
						if (Entry.Status != Reaction.ResultingStatus)
						{
							const float CarrierRemaining = Entry.ServerEndTime - CurrentTime;
							if (CarrierRemaining > 0.0f)
							{
								NewDuration = CarrierRemaining;
							}
						}
					}
				}

				if (FSurfaceCellStatusEntry* ResEntry = InOutCellData.FindStatus(Reaction.ResultingStatus))
				{
					ResEntry->ServerEndTime = FMath::Max(ResEntry->ServerEndTime, CurrentTime + NewDuration);
					ResEntry->Instigator = Instigator;
				}
				else
				{
					InOutCellData.ActiveStatuses.Add({ Reaction.ResultingStatus, CurrentTime + NewDuration, Instigator });
				}
				Result.bAccepted = true;
				Result.bStateModified = true;
			}
		}
		// B. Jeśli przychodzący status nie został skonsumowany (np. Conductive Shock: woda i prąd; Oil Ignition: olej i ogień)
		else if (!Reaction.bConsumeIncomingStatus)
		{
			if (CanMaterialReceiveStatus(InOutCellData.SurfaceMaterial, IncomingStatus, ActiveStatusesBefore))
			{
				float FinalDuration = (Duration > 0.0f) ? Duration : GetEffectConfig(IncomingStatus).GetBaseDuration();
				if (Reaction.bSyncWithCarrierDuration)
				{
					// Synchronizacja z czasem nośnika w komórce: status zależny trwa dokładnie tyle, ile pozostało nośnika
					for (const FSurfaceCellStatusEntry& Entry : InOutCellData.ActiveStatuses)
					{
						if (Entry.Status != IncomingStatus && DoesStatusSyncWithCarrier(IncomingStatus, Entry.Status))
						{
							const float CarrierRemaining = Entry.ServerEndTime - CurrentTime;
							if (CarrierRemaining > 0.0f)
							{
								FinalDuration = CarrierRemaining;
							}
						}
					}
				}

				if (FSurfaceCellStatusEntry* ExistingIncoming = InOutCellData.FindStatus(IncomingStatus))
				{
					ExistingIncoming->ServerEndTime = FMath::Max(ExistingIncoming->ServerEndTime, CurrentTime + FinalDuration);
					ExistingIncoming->Instigator = Instigator;
				}
				else
				{
					InOutCellData.ActiveStatuses.Add({ IncomingStatus, CurrentTime + FinalDuration, Instigator });
				}
				Result.bAccepted = true;
				Result.bStateModified = true;
			}
		}

		// C. Usunięcie wygaszonego/skonsumowanego statusu
		if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
		{
			InOutCellData.RemoveStatus(Reaction.ExistingStatusToRemove);
			Result.bStateModified = true;

			// D. Wycofanie osieroconych statusów zależnych (chronimy nowo dodany status wynikowy)
			CleanOrphanedStatuses(InOutCellData, Reaction.ResultingStatus);
		}

		// E. ZŁOTA ZASADA CHEMICZNA: Komórka nigdy nie może posiadać dwóch płynów jednocześnie.
		const EStatusEffectType DominantLiquid = IsLiquidStatus(Reaction.ResultingStatus) ? Reaction.ResultingStatus : (IsLiquidStatus(IncomingStatus) && !Reaction.bConsumeIncomingStatus ? IncomingStatus : EStatusEffectType::None);
		if (DominantLiquid != EStatusEffectType::None)
		{
			for (int32 Index = InOutCellData.ActiveStatuses.Num() - 1; Index >= 0; --Index)
			{
				if (InOutCellData.ActiveStatuses[Index].Status != DominantLiquid && IsLiquidStatus(InOutCellData.ActiveStatuses[Index].Status))
				{
					InOutCellData.ActiveStatuses.RemoveAt(Index);
					Result.bStateModified = true;
				}
			}
		}

		if (Result.bStateModified)
		{
			Result.bAccepted = true;
		}

		if (InOutCellData.IsEmpty())
		{
			Result.bCellBecameEmpty = true;
			Result.bAccepted = true;
			Result.bStateModified = true;
			return Result;
		}

		return Result;
	}

	// 4. Brak reakcji: sprawdzamy czy materiał pozwala na koegzystencję nowego statusu z obecnymi powłokami
	if (!InOutCellData.HasStatus(IncomingStatus))
	{
		if (CanMaterialReceiveStatus(InOutCellData.SurfaceMaterial, IncomingStatus, ActiveStatusesBefore))
		{
			// ZŁOTA ZASADA: Jeśli dodajemy płyn, usuwamy wszelkie inne płyny
			if (IsLiquidStatus(IncomingStatus))
			{
				for (int32 Index = InOutCellData.ActiveStatuses.Num() - 1; Index >= 0; --Index)
				{
					if (InOutCellData.ActiveStatuses[Index].Status != IncomingStatus && IsLiquidStatus(InOutCellData.ActiveStatuses[Index].Status))
					{
						InOutCellData.ActiveStatuses.RemoveAt(Index);
					}
				}
			}

			InOutCellData.ActiveStatuses.Add({ IncomingStatus, CurrentTime + Duration, Instigator });
			Result.bAccepted = true;
			Result.bStateModified = true;
			return Result;
		}
	}

	return Result;
}


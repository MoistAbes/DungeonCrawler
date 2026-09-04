#include "StatusEffectComponent.h"

#include "GameFramework/Actor.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

UStatusEffectComponent::UStatusEffectComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UStatusEffectComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        DamageableComponent = Owner->FindComponentByClass<UDamageableComponent>();
    }
}

void UStatusEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ActiveEffects.Num() == 0)
    {
        SetComponentTickEnabled(false);
        return;
    }

    TArray<EStatusEffectType> ExpiredEffects;

    for (auto& Pair : ActiveEffects)
    {
        FActiveStatusEffectInstance& Instance = Pair.Value;
        Instance.RemainingDuration -= DeltaTime;

        // Okresowe tyknięcie (np. DoT od ognia)
        Instance.TimeUntilNextTick -= DeltaTime;
        if (Instance.TimeUntilNextTick <= 0.0f)
        {
            Instance.TimeUntilNextTick += Instance.TickInterval;

            if (Instance.EffectType == EStatusEffectType::Burning && DamageableComponent)
            {
                const float TickDamage = BurnDamagePerSecond * Instance.TickInterval;
                DamageableComponent->ApplyDamage(TickDamage);
            }
        }

        if (Instance.RemainingDuration <= 0.0f)
        {
            ExpiredEffects.Add(Pair.Key);
        }
    }

    for (EStatusEffectType Expired : ExpiredEffects)
    {
        RemoveStatus(Expired);
    }
}

bool UStatusEffectComponent::ApplyStatus(EStatusEffectType NewStatus, float Duration, AActor* InstigatorActor)
{
    if (NewStatus == EStatusEffectType::None || Duration <= 0.0f)
    {
        return false;
    }

    const EPhysicalMaterialType OwnerMaterial = GetOwnerMaterialType();
    const TArray<EStatusEffectType> ActiveStatusList = GetActiveStatuses();

    // 1. Walidacja tożsamości materiałowej celu przez dedykowany silnik chemii
    if (!UElementalChemistryLibrary::CanMaterialReceiveStatus(OwnerMaterial, NewStatus, ActiveStatusList))
    {
        UE_LOG(LogTemp, Log, TEXT("[StatusEffect] %s cannot receive %s (Material %d incompatible)"),
            *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), static_cast<int32>(OwnerMaterial));
        return false;
    }

    // 2. Ewaluacja reakcji żywiołowych przez dedykowany silnik chemii
    const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(NewStatus, ActiveStatusList);
    if (Reaction.bReactionOccurred)
    {
        // Usunięcie skonsumowanego statusu
        if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
        {
            RemoveStatus(Reaction.ExistingStatusToRemove);
        }

        // Zadanie natychmiastowych obrażeń reakcji (np. wybuch oleju, szok elektryczny)
        if (Reaction.BonusInstantDamage > 0.0f && DamageableComponent)
        {
            DamageableComponent->ApplyDamage(Reaction.BonusInstantDamage);
        }

        UE_LOG(LogTemp, Warning, TEXT("[StatusReaction] %s: Triggered '%s'!"),
            *GetOwner()->GetName(), *Reaction.ReactionTag.ToString());

        OnElementalReactionTriggered.Broadcast(NewStatus, Reaction.ExistingStatusToRemove, Reaction.ReactionTag);

        // Jeśli reakcja zneutralizowała przychodzący żywioł (np. woda zgasiła ogień)
        if (Reaction.bConsumeIncomingStatus)
        {
            UpdateTickState();
            return true;
        }
    }

    // 3. Brak duplikatów: jeśli status już trwa, odświeżamy czas trwania
    if (FActiveStatusEffectInstance* Existing = ActiveEffects.Find(NewStatus))
    {
        Existing->RemainingDuration = FMath::Max(Existing->RemainingDuration, Duration);
        Existing->TotalDuration = FMath::Max(Existing->TotalDuration, Duration);
        if (InstigatorActor)
        {
            Existing->InstigatorActor = InstigatorActor;
        }

        UE_LOG(LogTemp, Log, TEXT("[StatusEffect] %s refreshed status %s (Remaining: %.1fs)"),
            *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Existing->RemainingDuration);

        OnStatusEffectApplied.Broadcast(NewStatus, Existing->RemainingDuration);
        return true;
    }

    // 4. Nałożenie nowej instancji statusu
    FActiveStatusEffectInstance NewInstance;
    NewInstance.EffectType = NewStatus;
    NewInstance.RemainingDuration = Duration;
    NewInstance.TotalDuration = Duration;
    NewInstance.TickInterval = (NewStatus == EStatusEffectType::Burning) ? BurnTickInterval : 1.0f;
    NewInstance.TimeUntilNextTick = NewInstance.TickInterval;
    NewInstance.InstigatorActor = InstigatorActor;

    ActiveEffects.Add(NewStatus, NewInstance);
    UpdateTickState();

    UE_LOG(LogTemp, Warning, TEXT("[StatusEffect] %s GAINED status: %s (Duration: %.1fs)"),
        *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Duration);

    OnStatusEffectApplied.Broadcast(NewStatus, Duration);
    return true;
}

bool UStatusEffectComponent::RemoveStatus(EStatusEffectType StatusToRemove)
{
    if (!ActiveEffects.Contains(StatusToRemove))
    {
        return false;
    }

    ActiveEffects.Remove(StatusToRemove);
    UpdateTickState();

    UE_LOG(LogTemp, Log, TEXT("[StatusEffect] %s LOST status: %s"),
        *GetOwner()->GetName(), *UEnum::GetValueAsString(StatusToRemove));

    OnStatusEffectRemoved.Broadcast(StatusToRemove);
    return true;
}

void UStatusEffectComponent::ClearAllStatuses()
{
    TArray<EStatusEffectType> Keys;
    ActiveEffects.GetKeys(Keys);

    for (EStatusEffectType Key : Keys)
    {
        RemoveStatus(Key);
    }
}

bool UStatusEffectComponent::HasStatus(EStatusEffectType Status) const
{
    return ActiveEffects.Contains(Status);
}

float UStatusEffectComponent::GetRemainingDuration(EStatusEffectType Status) const
{
    if (const FActiveStatusEffectInstance* Found = ActiveEffects.Find(Status))
    {
        return Found->RemainingDuration;
    }
    return 0.0f;
}

float UStatusEffectComponent::GetTotalDuration(EStatusEffectType Status) const
{
    if (const FActiveStatusEffectInstance* Found = ActiveEffects.Find(Status))
    {
        return Found->TotalDuration;
    }
    return 0.0f;
}

TArray<EStatusEffectType> UStatusEffectComponent::GetActiveStatuses() const
{
    TArray<EStatusEffectType> Keys;
    ActiveEffects.GetKeys(Keys);
    return Keys;
}

EPhysicalMaterialType UStatusEffectComponent::GetOwnerMaterialType() const
{
    if (const AActor* Owner = GetOwner())
    {
        if (Owner->Implements<UMaterialProviderInterface>())
        {
            return IMaterialProviderInterface::Execute_GetMaterialType(Owner);
        }
    }
    return EPhysicalMaterialType::Default;
}

void UStatusEffectComponent::UpdateTickState()
{
    const bool bShouldTick = (ActiveEffects.Num() > 0);
    if (IsComponentTickEnabled() != bShouldTick)
    {
        SetComponentTickEnabled(bShouldTick);
    }
}

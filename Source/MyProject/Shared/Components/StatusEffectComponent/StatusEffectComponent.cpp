#include "StatusEffectComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "MyProject/Environment/Elements/Data/StatusEffectDefinitions.h"
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

        // Okresowe tyknięcie (np. DoT pobierany z rejestru definicji)
        Instance.TimeUntilNextTick -= DeltaTime;
        if (Instance.TimeUntilNextTick <= 0.0f)
        {
            Instance.TimeUntilNextTick += Instance.TickInterval;

            const FStatusEffectDefinition& Def = FStatusEffectRegistry::GetDefinition(Instance.EffectType);
            if (Def.DamagePerSecond > 0.0f && DamageableComponent)
            {
                const float TickDamage = Def.DamagePerSecond * Instance.TickInterval;
                DamageableComponent->ApplyDamage(TickDamage);
            }
        }

        if (Instance.RemainingDuration <= 0.0f)
        {
            ExpiredEffects.Add(Pair.Key);
        }
    }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    // Dynamiczny podgląd 3D nad obiektem w świecie gry
    if (bShowDebugInWorld && GetWorld())
    {
        if (const AActor* Owner = GetOwner())
        {
            FVector Origin, BoxExtent;
            Owner->GetActorBounds(true, Origin, BoxExtent);
            const FVector BaseLocation = Origin + FVector(0.0f, 0.0f, BoxExtent.Z + 15.0f);

            int32 StackIndex = 0;
            for (const auto& Pair : ActiveEffects)
            {
                const EStatusEffectType Status = Pair.Key;
                const FActiveStatusEffectInstance& Inst = Pair.Value;

                FColor StatusColor = FColor::White;
                FString StatusName = TEXT("Status");

                switch (Status)
                {
                case EStatusEffectType::Burning:
                    StatusColor = FColor(255, 60, 0);
                    StatusName = TEXT("🔥 BURNING");
                    break;
                case EStatusEffectType::Wet:
                    StatusColor = FColor(0, 180, 255);
                    StatusName = TEXT("💧 WET");
                    break;
                case EStatusEffectType::Electrified:
                    StatusColor = FColor(255, 230, 0);
                    StatusName = TEXT("⚡ ELECTRIFIED");
                    break;
                case EStatusEffectType::Oiled:
                    StatusColor = FColor(180, 110, 40);
                    StatusName = TEXT("🛢️ OILED");
                    break;
                default:
                    break;
                }

                const FString DebugStr = FString::Printf(TEXT("%s (%.1fs)"), *StatusName, Inst.RemainingDuration);
                const FVector DrawPos = BaseLocation + FVector(0.0f, 0.0f, StackIndex * 22.0f);

                DrawDebugString(GetWorld(), DrawPos, DebugStr, nullptr, StatusColor, 0.0f, true, 1.2f);
                StackIndex++;
            }
        }
    }
#endif

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
        // Usunięcie skonsumowanego/wypartego statusu
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

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        if (bShowDebugInWorld && GetWorld() && GetOwner())
        {
            const FVector ReactionPos = GetOwner()->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
            DrawDebugString(GetWorld(), ReactionPos, FString::Printf(TEXT("💥 REACTION: %s!"), *Reaction.ReactionTag.ToString()), nullptr, FColor::Magenta, 2.5f, true, 1.4f);
        }
        if (GEngine && GetOwner())
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Magenta,
                FString::Printf(TEXT("[%s] REACTION: %s!"), *GetOwner()->GetName(), *Reaction.ReactionTag.ToString()));
        }
#endif

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

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        if (GEngine && GetOwner())
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
                FString::Printf(TEXT("[%s] REFRESHED: %s (%.1fs)"), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Existing->RemainingDuration));
        }
#endif

        OnStatusEffectApplied.Broadcast(NewStatus, Existing->RemainingDuration);
        return true;
    }

    // 4. Nałożenie nowej instancji statusu na podstawie rejestru definicji
    const FStatusEffectDefinition& Def = FStatusEffectRegistry::GetDefinition(NewStatus);

    FActiveStatusEffectInstance NewInstance;
    NewInstance.EffectType = NewStatus;
    NewInstance.RemainingDuration = Duration;
    NewInstance.TotalDuration = Duration;
    NewInstance.TickInterval = (Def.TickInterval > 0.0f) ? Def.TickInterval : 1.0f;
    NewInstance.TimeUntilNextTick = NewInstance.TickInterval;
    NewInstance.InstigatorActor = InstigatorActor;

    ActiveEffects.Add(NewStatus, NewInstance);
    UpdateTickState();

    UE_LOG(LogTemp, Warning, TEXT("[StatusEffect] %s GAINED status: %s (Duration: %.1fs)"),
        *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Duration);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (GEngine && GetOwner())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
            FString::Printf(TEXT("[%s] GAINED: %s (%.1fs)"), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Duration));
    }
#endif

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

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (GEngine && GetOwner())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
            FString::Printf(TEXT("[%s] LOST: %s"), *GetOwner()->GetName(), *UEnum::GetValueAsString(StatusToRemove)));
    }
#endif

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
    TArray<EStatusEffectType> Result;
    ActiveEffects.GetKeys(Result);
    return Result;
}

EPhysicalMaterialType UStatusEffectComponent::GetOwnerMaterialType() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return EPhysicalMaterialType::Flesh;
    }

    if (Owner->Implements<UMaterialProviderInterface>())
    {
        return IMaterialProviderInterface::Execute_GetMaterialType(Owner);
    }

    return EPhysicalMaterialType::Flesh;
}

void UStatusEffectComponent::UpdateTickState()
{
    const bool bShouldTick = (ActiveEffects.Num() > 0);
    SetComponentTickEnabled(bShouldTick);
}

#include "StatusEffectComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Elements/Data/StatusEffectDefinitions.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

UStatusEffectComponent::UStatusEffectComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

void UStatusEffectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UStatusEffectComponent, ActiveStatusEffects);
}

void UStatusEffectComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        DamageableComponent = Owner->FindComponentByClass<UDamageableComponent>();
    }

    UpdateTickState();
}

void UStatusEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Klienci nie przetwarzają DoT ani wygasania - jedynie renderują etykiety podglądu
    if (!NetUtils::HasAuthority(this))
    {
        DrawDebugLabels();
        return;
    }

    if (ActiveStatusEffects.IsEmpty())
    {
        UpdateTickState();
        return;
    }

    const float CurrentTime = GetCurrentSyncedTime();
    TArray<EStatusEffectType> ExpiredEffects;

    for (FActiveStatusEffectInstance& Instance : ActiveStatusEffects)
    {
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

        // Weryfikacja wygaśnięcia czasu trwania (Zero-Bandwidth pattern)
        if (CurrentTime >= Instance.ServerEndTime)
        {
            ExpiredEffects.Add(Instance.EffectType);
        }
    }

    DrawDebugLabels();

    for (EStatusEffectType Expired : ExpiredEffects)
    {
        RemoveStatus(Expired);
    }
}

bool UStatusEffectComponent::ApplyStatus(EStatusEffectType NewStatus, float Duration, AActor* InstigatorActor)
{
    REQUIRE_AUTHORITY_RET(false);

    if (NewStatus == EStatusEffectType::None || Duration <= 0.0f)
    {
        return false;
    }

    const EPhysicalMaterialType OwnerMaterial = GetOwnerMaterialType();
    const TArray<EStatusEffectType> ActiveStatusList = GetActiveStatuses();

    // 1. Reakcje chemiczne żywiołów (np. Vaporize, Extinguish, Oil Ignition)
    const bool bConsumed = ProcessElementalReaction(NewStatus, ActiveStatusList);
    if (bConsumed)
    {
        return true;
    }

    // 2. Walidacja tożsamości materiałowej celu: czy materiał może utrzymać ten status?
    // Przekazujemy listę powłok z momentu uderzenia (np. naoliwiony kamień pozwala na podtrzymanie ognia)
    if (!UElementalChemistryLibrary::CanMaterialReceiveStatus(OwnerMaterial, NewStatus, ActiveStatusList))
    {
        UE_LOG(LogTemp, Log, TEXT("[StatusEffect]%s %s cannot sustain %s (Material %d incompatible)"),
            *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), static_cast<int32>(OwnerMaterial));
        return false;
    }

    const float NewEndTime = GetCurrentSyncedTime() + Duration;

    // 3. Odświeżenie istniejącego lub zarejestrowanie nowego statusu
    if (FActiveStatusEffectInstance* Existing = FindInstance(NewStatus))
    {
        RefreshExistingStatus(*Existing, Duration, NewEndTime, InstigatorActor);
    }
    else
    {
        AddNewStatusInstance(NewStatus, Duration, NewEndTime, InstigatorActor);
    }

    return true;
}

bool UStatusEffectComponent::ProcessElementalReaction(EStatusEffectType NewStatus, const TArray<EStatusEffectType>& ActiveStatuses)
{
    const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(NewStatus, ActiveStatuses);
    if (!Reaction.bReactionOccurred)
    {
        return false;
    }

    // Usunięcie skonsumowanego/wypartego statusu (np. woda odparowuje od ognia, olej spala się)
    if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
    {
        RemoveStatus(Reaction.ExistingStatusToRemove);
    }

    // Zadanie natychmiastowych obrażeń reakcji (np. wybuch oleju, szok elektryczny)
    if (Reaction.BonusInstantDamage > 0.0f && DamageableComponent)
    {
        DamageableComponent->ApplyDamage(Reaction.BonusInstantDamage);
    }

    UE_LOG(LogTemp, Warning, TEXT("[StatusReaction]%s %s: Triggered '%s'!"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *Reaction.ReactionTag.ToString());

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

    // Jeśli reakcja całkowicie zneutralizowała przychodzący żywioł (np. woda zgasiła ogień)
    if (Reaction.bConsumeIncomingStatus)
    {
        UpdateTickState();
        return true;
    }

    return false;
}

void UStatusEffectComponent::RefreshExistingStatus(FActiveStatusEffectInstance& Existing, float Duration, float NewEndTime, AActor* InstigatorActor)
{
    Existing.ServerEndTime = FMath::Max(Existing.ServerEndTime, NewEndTime);
    Existing.TotalDuration = FMath::Max(Existing.TotalDuration, Duration);
    if (InstigatorActor)
    {
        Existing.InstigatorActor = InstigatorActor;
    }

    UE_LOG(LogTemp, Log, TEXT("[StatusEffect]%s %s refreshed status %s (Remaining: %.1fs)"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(Existing.EffectType), GetRemainingDuration(Existing.EffectType));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (GEngine && GetOwner())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
            FString::Printf(TEXT("[%s] REFRESHED: %s (%.1fs)"), *GetOwner()->GetName(), *UEnum::GetValueAsString(Existing.EffectType), GetRemainingDuration(Existing.EffectType)));
    }
#endif

    OnStatusEffectApplied.Broadcast(Existing.EffectType, GetRemainingDuration(Existing.EffectType));
}

void UStatusEffectComponent::AddNewStatusInstance(EStatusEffectType NewStatus, float Duration, float NewEndTime, AActor* InstigatorActor)
{
    const FStatusEffectDefinition& Def = FStatusEffectRegistry::GetDefinition(NewStatus);

    FActiveStatusEffectInstance NewInstance;
    NewInstance.EffectType = NewStatus;
    NewInstance.TotalDuration = Duration;
    NewInstance.ServerEndTime = NewEndTime;
    NewInstance.TickInterval = (Def.TickInterval > 0.0f) ? Def.TickInterval : 1.0f;
    NewInstance.TimeUntilNextTick = NewInstance.TickInterval;
    NewInstance.InstigatorActor = InstigatorActor;

    ActiveStatusEffects.Add(NewInstance);
    UpdateTickState();

    UE_LOG(LogTemp, Warning, TEXT("[StatusEffect]%s %s GAINED status: %s (Duration: %.1fs)"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Duration);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (GEngine && GetOwner())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
            FString::Printf(TEXT("[%s] GAINED: %s (%.1fs)"), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Duration));
    }
#endif

    OnStatusEffectApplied.Broadcast(NewStatus, Duration);
}

bool UStatusEffectComponent::RemoveStatus(EStatusEffectType StatusToRemove)
{
    REQUIRE_AUTHORITY_RET(false);

    const int32 Index = ActiveStatusEffects.IndexOfByPredicate([StatusToRemove](const FActiveStatusEffectInstance& Item)
    {
        return Item.EffectType == StatusToRemove;
    });

    if (Index == INDEX_NONE)
    {
        return false;
    }

    ActiveStatusEffects.RemoveAt(Index);
    UpdateTickState();

    UE_LOG(LogTemp, Log, TEXT("[StatusEffect]%s %s LOST status: %s"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(StatusToRemove));

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
    REQUIRE_AUTHORITY();

    const TArray<EStatusEffectType> CurrentTypes = GetActiveStatuses();
    for (EStatusEffectType Status : CurrentTypes)
    {
        RemoveStatus(Status);
    }
}

bool UStatusEffectComponent::HasStatus(EStatusEffectType Status) const
{
    return FindInstance(Status) != nullptr;
}

float UStatusEffectComponent::GetRemainingDuration(EStatusEffectType Status) const
{
    if (const FActiveStatusEffectInstance* Found = FindInstance(Status))
    {
        return FMath::Max(0.0f, Found->ServerEndTime - GetCurrentSyncedTime());
    }
    return 0.0f;
}

float UStatusEffectComponent::GetTotalDuration(EStatusEffectType Status) const
{
    if (const FActiveStatusEffectInstance* Found = FindInstance(Status))
    {
        return Found->TotalDuration;
    }
    return 0.0f;
}

TArray<EStatusEffectType> UStatusEffectComponent::GetActiveStatuses() const
{
    TArray<EStatusEffectType> Result;
    Result.Reserve(ActiveStatusEffects.Num());
    for (const FActiveStatusEffectInstance& Item : ActiveStatusEffects)
    {
        Result.Add(Item.EffectType);
    }
    return Result;
}

void UStatusEffectComponent::OnRep_ActiveStatusEffects(const TArray<FActiveStatusEffectInstance>& OldEffects)
{
    // 1. Wykryj usunięte statusy (obecne w OldEffects, ale brak w ActiveStatusEffects)
    for (const FActiveStatusEffectInstance& Old : OldEffects)
    {
        if (!HasStatus(Old.EffectType))
        {
            OnStatusEffectRemoved.Broadcast(Old.EffectType);
        }
    }

    // 2. Wykryj nowo dodane lub odświeżone statusy
    for (const FActiveStatusEffectInstance& Current : ActiveStatusEffects)
    {
        const FActiveStatusEffectInstance* Old = OldEffects.FindByPredicate([Current](const FActiveStatusEffectInstance& Item)
        {
            return Item.EffectType == Current.EffectType;
        });

        if (!Old)
        {
            // Nowo nałożony status u klienta
            OnStatusEffectApplied.Broadcast(Current.EffectType, GetRemainingDuration(Current.EffectType));
        }
        else if (!FMath::IsNearlyEqual(Old->ServerEndTime, Current.ServerEndTime, 0.05f))
        {
            // Odświeżony czas trwania
            OnStatusEffectApplied.Broadcast(Current.EffectType, GetRemainingDuration(Current.EffectType));
        }
    }

    UpdateTickState();
}

const FActiveStatusEffectInstance* UStatusEffectComponent::FindInstance(EStatusEffectType Status) const
{
    return ActiveStatusEffects.FindByPredicate([Status](const FActiveStatusEffectInstance& Item)
    {
        return Item.EffectType == Status;
    });
}

FActiveStatusEffectInstance* UStatusEffectComponent::FindInstance(EStatusEffectType Status)
{
    return ActiveStatusEffects.FindByPredicate([Status](const FActiveStatusEffectInstance& Item)
    {
        return Item.EffectType == Status;
    });
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

float UStatusEffectComponent::GetCurrentSyncedTime() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const AGameStateBase* GS = World->GetGameState())
        {
            return GS->GetServerWorldTimeSeconds();
        }
        return World->GetTimeSeconds();
    }
    return 0.0f;
}

void UStatusEffectComponent::UpdateTickState()
{
    const bool bHasEffects = (ActiveStatusEffects.Num() > 0);
    const bool bIsServer = NetUtils::HasAuthority(this);

    bool bShouldTick = false;
    if (bIsServer)
    {
        bShouldTick = bHasEffects;
    }
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    else if (bShowDebugInWorld && bHasEffects)
    {
        bShouldTick = true;
    }
#endif

    SetComponentTickEnabled(bShouldTick);
}

void UStatusEffectComponent::DrawDebugLabels() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (!bShowDebugInWorld || !GetWorld()) return;

    const AActor* Owner = GetOwner();
    if (!Owner) return;

    FVector Origin, BoxExtent;
    Owner->GetActorBounds(true, Origin, BoxExtent);
    const FVector BaseLocation = Origin + FVector(0.0f, 0.0f, BoxExtent.Z + 15.0f);

    int32 StackIndex = 0;
    for (const FActiveStatusEffectInstance& Inst : ActiveStatusEffects)
    {
        const EStatusEffectType Status = Inst.EffectType;

        FColor StatusColor = FColor::White;
        switch (Status)
        {
        case EStatusEffectType::Burning:
            StatusColor = FColor(255, 60, 0);
            break;
        case EStatusEffectType::Wet:
            StatusColor = FColor(0, 180, 255);
            break;
        case EStatusEffectType::Electrified:
            StatusColor = FColor(255, 230, 0);
            break;
        case EStatusEffectType::Oiled:
            StatusColor = FColor(180, 110, 40);
            break;
        default:
            break;
        }

        const FString StatusName = UEnum::GetDisplayValueAsText(Status).ToString().ToUpper();
        const float Remaining = GetRemainingDuration(Status);
        const FString DebugStr = FString::Printf(TEXT("%s (%.1fs)"), *StatusName, Remaining);
        const FVector DrawPos = BaseLocation + FVector(0.0f, 0.0f, StackIndex * 22.0f);

        DrawDebugString(GetWorld(), DrawPos, DebugStr, nullptr, StatusColor, 0.0f, true, 1.2f);
        StackIndex++;
    }
#endif
}

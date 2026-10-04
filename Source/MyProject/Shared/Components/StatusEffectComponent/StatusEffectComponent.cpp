#include "StatusEffectComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h"

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

    if (UWorld* World = GetWorld())
    {
        if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
        {
            SurfaceSubsystem->RegisterStatusComponent(this);
        }
    }

    UpdateTickState();
}

void UStatusEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
        {
            SurfaceSubsystem->UnregisterStatusComponent(this);
        }
    }

    Super::EndPlay(EndPlayReason);
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
        // Okresowe tyknięcie (np. DoT pobierany z rejestru konfiguracji) z akumulatorem nadrabiającym opóźnienia
        Instance.TimeUntilNextTick -= DeltaTime;
        if (Instance.TickInterval <= 0.0f)
        {
            Instance.TickInterval = 1.0f;
        }

        int32 TicksApplied = 0;
        while (Instance.TimeUntilNextTick <= 0.0f && TicksApplied < 3)
        {
            Instance.TimeUntilNextTick += Instance.TickInterval;
            TicksApplied++;

            const FStatusEffectConfig& Def = UElementalReactionRules::GetEffectConfig(Instance.EffectType);
            if (!Def.bIsDoTType)
            {
                break;
            }

            const float DamagePerSec = Def.GetDamagePerSecond(Instance.Tier);
            if (DamagePerSec > 0.0f && DamageableComponent)
            {
                const float TickDamage = DamagePerSec * Instance.TickInterval;
                const EDamageType DamageType = PhysicalMaterialUtils::StatusToDamageType(Instance.EffectType);
                DamageableComponent->ApplyDamage(TickDamage, DamageType, Instance.InstigatorActor.Get());

                if (!IsValid(GetOwner()) || !IsValid(DamageableComponent))
                {
                    break;
                }
            }
        }

        // Anti-runaway clamp: przy hitchach > 3s resetujemy interwał, zapobiegając lawinie ticków w kolejnej klatce
        if (Instance.TimeUntilNextTick <= 0.0f)
        {
            Instance.TimeUntilNextTick = Instance.TickInterval;
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

float UStatusEffectComponent::ComputeAdjustedDuration(EStatusEffectType Status, float BaseDuration, bool bSyncWithCarrier) const
{
    const EPhysicalMaterialType Material = GetOwnerMaterialType();
    const float CurrentTime = GetCurrentSyncedTime();
    float FinalDuration = BaseDuration;

    for (const FActiveStatusEffectInstance& ActiveInst : ActiveStatusEffects)
    {
        const float CarrierRemaining = ActiveInst.ServerEndTime - CurrentTime;
        FinalDuration = UElementalReactionRules::ApplyCarrierSync(
            Material,
            Status,
            FinalDuration,
            ActiveInst.EffectType,
            CarrierRemaining,
            bSyncWithCarrier);
    }

    return FinalDuration;
}

void UStatusEffectComponent::DisplaceOtherLiquids(EStatusEffectType IncomingLiquid)
{
    if (!UElementalReactionRules::IsLiquidStatus(IncomingLiquid))
    {
        return;
    }

    for (int32 Idx = ActiveStatusEffects.Num() - 1; Idx >= 0; --Idx)
    {
        if (ActiveStatusEffects[Idx].EffectType != IncomingLiquid && UElementalReactionRules::IsLiquidStatus(ActiveStatusEffects[Idx].EffectType))
        {
            UE_LOG(LogDungeonElements, Log, TEXT("[StatusEffect] Displacing existing liquid %s with incoming liquid %s on %s"),
                *UEnum::GetValueAsString(ActiveStatusEffects[Idx].EffectType), *UEnum::GetValueAsString(IncomingLiquid), *GetOwner()->GetName());
            RemoveStatus(ActiveStatusEffects[Idx].EffectType);
        }
    }
}

void UStatusEffectComponent::SyncDependentStatusesWithCarrier(EStatusEffectType CarrierStatus, float CarrierEndTime)
{
    for (FActiveStatusEffectInstance& OtherInst : ActiveStatusEffects)
    {
        if (OtherInst.EffectType != CarrierStatus && UElementalReactionRules::DoesStatusSyncWithCarrier(OtherInst.EffectType, CarrierStatus))
        {
            OtherInst.ServerEndTime = FMath::Max(OtherInst.ServerEndTime, CarrierEndTime);
        }
    }
}

void UStatusEffectComponent::UpsertStatus(EStatusEffectType Status, uint8 Tier, float Duration, float EndTime, AActor* InstigatorActor)
{
    if (FActiveStatusEffectInstance* Existing = FindInstance(Status))
    {
        RefreshExistingStatus(*Existing, Tier, Duration, EndTime, InstigatorActor);
    }
    else
    {
        AddNewStatusInstance(Status, Tier, Duration, EndTime, InstigatorActor);
    }
}

bool UStatusEffectComponent::ApplyStatus(EStatusEffectType NewStatus, uint8 Tier, float OverrideDuration, AActor* InstigatorActor)
{
    REQUIRE_AUTHORITY_RET(false);

    if (NewStatus == EStatusEffectType::None)
    {
        return false;
    }

    const FStatusEffectConfig& Config = UElementalReactionRules::GetEffectConfig(NewStatus);
    const float Duration = (OverrideDuration > 0.0f) ? OverrideDuration : Config.GetBaseDuration(Tier);
    if (Duration <= 0.0f)
    {
        return false;
    }

    const float CurrentTime = GetCurrentSyncedTime();

    // 1. Zbuduj migawkę aktywnych statusów obiektu
    TArray<FElementalActiveStatusSnapshot> Snapshots;
    for (const FActiveStatusEffectInstance& Inst : ActiveStatusEffects)
    {
        Snapshots.Add({
            Inst.EffectType,
            Inst.Tier,
            FMath::Max(0.0f, Inst.ServerEndTime - CurrentTime),
            false
        });
    }

    // 2. Wywołaj centralny kalkulator przejścia (Single Source of Truth)
    const FElementalTransitionPlan Plan = UElementalReactionRules::CalculateElementalTransition(
        GetOwnerMaterialType(),
        NewStatus,
        Duration,
        Tier,
        Snapshots);

    if (!Plan.bAccepted)
    {
        UE_LOG(LogDungeonElements, Verbose, TEXT("[StatusEffect]%s %s rejected status %s (Material %d incompatible)"),
            *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), static_cast<int32>(GetOwnerMaterialType()));
        return false;
    }

    // 3. Rozgłoś wszystkie reakcje żywiołowe, które odpaliły w łańcuchu (VFX, SFX, audio, delegat)
    if (Plan.PrimaryReaction.bReactionOccurred)
    {
        OnElementalReactionTriggered.Broadcast(NewStatus, Plan.PrimaryReaction.ExistingStatusToRemove, Plan.PrimaryReaction.ReactionTag);
    }
    for (FName ReactionTag : Plan.TriggeredReactionTags)
    {
        if (ReactionTag != Plan.PrimaryReaction.ReactionTag)
        {
            OnElementalReactionTriggered.Broadcast(NewStatus, EStatusEffectType::None, ReactionTag);
        }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        if (bShowDebugInWorld && GetWorld() && GetOwner())
        {
            const FVector ReactionPos = GetOwner()->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
            DrawDebugString(GetWorld(), ReactionPos, FString::Printf(TEXT("💥 REACTION: %s!"), *ReactionTag.ToString()), nullptr, FColor::Magenta, 2.5f, true, 1.4f);
        }
        if (bShowDebugOnScreen && GEngine && GetOwner())
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Magenta,
                FString::Printf(TEXT("[%s] REACTION: %s!"), *GetOwner()->GetName(), *ReactionTag.ToString()));
        }
#endif
    }

    // 4. Usuń statusy wskazane przez plan (np. zneutralizowana woda, rozładowany prąd)
    for (EStatusEffectType StatusToRemove : Plan.StatusesToRemove)
    {
        RemoveStatus(StatusToRemove);
    }

    // 5. Nałóż/odśwież statusy wskazane przez plan
    for (const FElementalStatusApplyInfo& ApplyInfo : Plan.StatusesToApply)
    {
        const float FinalEndTime = CurrentTime + ApplyInfo.Duration;
        UpsertStatus(ApplyInfo.Status, ApplyInfo.Tier, ApplyInfo.Duration, FinalEndTime, InstigatorActor);
        if (ApplyInfo.bSyncWithCarrierDuration || UElementalReactionRules::IsLiquidStatus(ApplyInfo.Status))
        {
            SyncDependentStatusesWithCarrier(ApplyInfo.Status, FinalEndTime);
        }
    }

    UpdateTickState();
    return true;
}

void UStatusEffectComponent::RefreshExistingStatus(FActiveStatusEffectInstance& Existing, uint8 Tier, float Duration, float NewEndTime, AActor* InstigatorActor)
{
    // ZŁOTA ZASADA CHEMICZNA: Jeśli odświeżany status jest płynem, żaden inny płyn nie może istnieć
    DisplaceOtherLiquids(Existing.EffectType);

    // Jeśli nowy tier jest wyższy, podnosimy tier
    if (Tier > Existing.Tier)
    {
        Existing.Tier = Tier;
        const FStatusEffectConfig& Def = UElementalReactionRules::GetEffectConfig(Existing.EffectType);
        const float NewTickInterval = Def.GetTickInterval(Tier);
        Existing.TickInterval = (NewTickInterval > 0.0f) ? NewTickInterval : 1.0f;
    }

    // Wyliczamy skorygowany czas trwania z uwzględnieniem nośnika (jeśli wymagany)
    const float AdjustedDuration = ComputeAdjustedDuration(Existing.EffectType, Duration, false);
    const float TargetEndTime = GetCurrentSyncedTime() + AdjustedDuration;

    Existing.ServerEndTime = FMath::Max(Existing.ServerEndTime, TargetEndTime);
    Existing.TotalDuration = FMath::Max(Existing.TotalDuration, Duration);
    if (InstigatorActor)
    {
        Existing.InstigatorActor = InstigatorActor;
    }

    // Synchronizacja czasu dla statusów zależnych od tego nośnika (np. prąd na mokrej postaci)
    SyncDependentStatusesWithCarrier(Existing.EffectType, Existing.ServerEndTime);

    UE_LOG(LogDungeonElements, Verbose, TEXT("[StatusEffect]%s %s refreshed status %s (Tier %d, Remaining: %.1fs)"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(Existing.EffectType), Existing.Tier, GetRemainingDuration(Existing.EffectType));

    OnStatusEffectApplied.Broadcast(Existing.EffectType, GetRemainingDuration(Existing.EffectType));
}

void UStatusEffectComponent::AddNewStatusInstance(EStatusEffectType NewStatus, uint8 Tier, float Duration, float NewEndTime, AActor* InstigatorActor)
{
    // ZŁOTA ZASADA CHEMICZNA: Całkowity zakaz koegzystencji dwóch płynów (Liquid Mutual Exclusivity).
    DisplaceOtherLiquids(NewStatus);

    const FStatusEffectConfig& Def = UElementalReactionRules::GetEffectConfig(NewStatus);
    const float ConfigTickInterval = Def.GetTickInterval(Tier);

    FActiveStatusEffectInstance NewInstance;
    NewInstance.EffectType = NewStatus;
    NewInstance.Tier = Tier;
    NewInstance.TotalDuration = Duration;
    NewInstance.ServerEndTime = NewEndTime;
    NewInstance.TickInterval = (ConfigTickInterval > 0.0f) ? ConfigTickInterval : 1.0f;
    NewInstance.TimeUntilNextTick = NewInstance.TickInterval;
    NewInstance.InstigatorActor = InstigatorActor;

    ActiveStatusEffects.Add(NewInstance);
    UpdateTickState();

    // Jeśli nowo dodany status jest nośnikiem (np. wylano olej na płonącą postać lub oblano wodą naelektryzowaną),
    // synchronizujemy czas trwania istniejących statusów zależnych od tego nośnika!
    SyncDependentStatusesWithCarrier(NewStatus, NewEndTime);

    UE_LOG(LogDungeonElements, Log, TEXT("[StatusEffect]%s %s GAINED status: %s (Tier %d, Duration: %.1fs)"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Tier, Duration);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (bShowDebugOnScreen && GEngine && GetOwner())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
            FString::Printf(TEXT("[%s] GAINED: %s T%d (%.1fs)"), *GetOwner()->GetName(), *UEnum::GetValueAsString(NewStatus), Tier, Duration));
    }
#endif

    OnStatusEffectApplied.Broadcast(NewStatus, Duration);
}

uint8 UStatusEffectComponent::GetStatusTier(EStatusEffectType Status) const
{
    if (const FActiveStatusEffectInstance* Found = FindInstance(Status))
    {
        return Found->Tier;
    }
    return 0;
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

    UE_LOG(LogDungeonElements, Log, TEXT("[StatusEffect]%s %s LOST status: %s"),
        *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(), *UEnum::GetValueAsString(StatusToRemove));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (bShowDebugOnScreen && GEngine && GetOwner())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
            FString::Printf(TEXT("[%s] LOST: %s"), *GetOwner()->GetName(), *UEnum::GetValueAsString(StatusToRemove)));
    }
#endif

    OnStatusEffectRemoved.Broadcast(StatusToRemove);

    // Czyszczenie osieroconych statusów: jeśli usunięto nośnik (np. olej lub wodę),
    // każdy pozostały status, który nie może legalnie istnieć na tym materiale bez nośnika,
    // zostaje natychmiastowo wygaszony.
    CleanOrphanedStatuses();

    return true;
}

bool UStatusEffectComponent::CleanOrphanedStatuses()
{
    REQUIRE_AUTHORITY_RET(false);

    if (bIsCleaningOrphans)
    {
        return false;
    }

    TGuardValue<bool> CleaningGuard(bIsCleaningOrphans, true);
    const EPhysicalMaterialType OwnerMaterial = GetOwnerMaterialType();

    bool bAnyEvicted = false;
    bool bNeedsRecheck = true;

    while (bNeedsRecheck)
    {
        bNeedsRecheck = false;
        const TArray<EStatusEffectType> CurrentStatuses = GetActiveStatuses();

        for (EStatusEffectType Status : CurrentStatuses)
        {
            if (!UElementalReactionRules::CanMaterialReceiveStatus(OwnerMaterial, Status, CurrentStatuses))
            {
                UE_LOG(LogDungeonElements, Log, TEXT("[StatusEffect]%s %s: Evicted orphaned status %s on material %s (carrier expired/removed)"),
                    *NetUtils::GetNetRolePrefix(this), *GetOwner()->GetName(),
                    *UEnum::GetValueAsString(Status), *UEnum::GetValueAsString(OwnerMaterial));

                RemoveStatus(Status);
                bAnyEvicted = true;
                bNeedsRecheck = true;
                break;
            }
        }
    }

    return bAnyEvicted;
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

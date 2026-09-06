#include "SurfaceStatusComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"

USurfaceStatusComponent::USurfaceStatusComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

void USurfaceStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(USurfaceStatusComponent, ActivePatches);
}

void USurfaceStatusComponent::BeginPlay()
{
    Super::BeginPlay();

    CachedDamageableComp = GetOwner() ? GetOwner()->FindComponentByClass<UDamageableComponent>() : nullptr;
    UpdateTickState();
}

void USurfaceStatusComponent::UpdateTickState()
{
    const bool bShouldTick = ActivePatches.Num() > 0;
    if (IsComponentTickEnabled() != bShouldTick)
    {
        SetComponentTickEnabled(bShouldTick);
    }
}

EPhysicalMaterialType USurfaceStatusComponent::GetOwnerMaterialType() const
{
    if (const AActor* Owner = GetOwner())
    {
        if (Owner->GetClass()->ImplementsInterface(UMaterialProviderInterface::StaticClass()))
        {
            return IMaterialProviderInterface::Execute_GetMaterialType(Owner);
        }
    }
    return EPhysicalMaterialType::Stone;
}

bool USurfaceStatusComponent::ApplySurfaceStatus(
    EStatusEffectType NewStatus,
    float Duration,
    const FVector& ImpactPoint,
    const FVector& ImpactNormal,
    float Radius,
    AActor* InstigatorActor)
{
    REQUIRE_AUTHORITY_RET(false);

    if (NewStatus == EStatusEffectType::None || Duration <= 0.0f || Radius <= 0.0f || !GetWorld())
    {
        return false;
    }

    // 1. Sprawdzamy podatność materiału struktury na dany żywioł
    TArray<EStatusEffectType> ExistingStatuses;
    for (const FSurfaceStatusPatch& P : ActivePatches)
    {
        ExistingStatuses.AddUnique(P.StatusType);
    }

    const EPhysicalMaterialType OwnerMat = GetOwnerMaterialType();
    if (!UElementalChemistryLibrary::CanMaterialReceiveStatus(OwnerMat, NewStatus, ExistingStatuses))
    {
        return false;
    }

    // 2. Szukamy nakładającej się plamy na tej samej powierzchni (ta sama strona ściany/filaru)
    int32 OverlappingIndex = INDEX_NONE;
    for (int32 i = 0; i < ActivePatches.Num(); ++i)
    {
        const FSurfaceStatusPatch& Patch = ActivePatches[i];
        const float Dist = FVector::Dist(Patch.ImpactPoint, ImpactPoint);
        const float NormalDot = FVector::DotProduct(Patch.ImpactNormal, ImpactNormal);

        // Nakładanie: w promieniu obu plam i wektor normalny zgodny (ta sama strona, kąt < 60 st.)
        if (Dist <= (Patch.Radius + Radius) * 0.7f && NormalDot > 0.5f)
        {
            OverlappingIndex = i;
            break;
        }
    }

    // 3. Jeśli trafiliśmy w istniejącą plamę - ewaluujemy reakcję żywiołową
    if (OverlappingIndex != INDEX_NONE)
    {
        FSurfaceStatusPatch& OverlapPatch = ActivePatches[OverlappingIndex];

        // Identyczny status - odświeżamy czas trwania
        if (OverlapPatch.StatusType == NewStatus)
        {
            OverlapPatch.ServerEndTime = FMath::Max(OverlapPatch.ServerEndTime, GetWorld()->GetTimeSeconds() + Duration);
            OverlapPatch.TotalDuration = FMath::Max(OverlapPatch.TotalDuration, Duration);
            return true;
        }

        const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(NewStatus, { OverlapPatch.StatusType });
        if (Reaction.bReactionOccurred)
        {
            OnSurfaceReactionTriggered.Broadcast(NewStatus, OverlapPatch.StatusType, Reaction.ReactionTag, ImpactPoint);

            // Specjalna obsługa zapłonu oleju: plama staje w płomieniach
            if (Reaction.ReactionTag == FName(TEXT("Oil_Ignition")))
            {
                OverlapPatch.StatusType = EStatusEffectType::Burning;
                OverlapPatch.ServerEndTime = GetWorld()->GetTimeSeconds() + Duration;
                OverlapPatch.TotalDuration = Duration;

                if (Reaction.BonusInstantDamage > 0.0f && CachedDamageableComp.IsValid())
                {
                    CachedDamageableComp->ApplyDamage(Reaction.BonusInstantDamage);
                }
                return true;
            }

            // Usunięcie poprzedniego statusu (np. woda gasi ogień)
            if (Reaction.ExistingStatusToRemove != EStatusEffectType::None)
            {
                const FSurfaceStatusPatch RemovedCopy = OverlapPatch;
                ActivePatches.RemoveAt(OverlappingIndex);
                OnSurfacePatchRemoved.Broadcast(RemovedCopy);
            }

            // Jeśli przychodzący status uległ neutralizacji (np. woda odparowała ogień)
            if (Reaction.bConsumeIncomingStatus)
            {
                UpdateTickState();
                return true;
            }
        }
        else if (UElementalChemistryLibrary::IsLiquidStatus(NewStatus) && UElementalChemistryLibrary::IsLiquidStatus(OverlapPatch.StatusType))
        {
            // Nowy płyn zastępuje stary płyn na powierzchni
            OverlapPatch.StatusType = NewStatus;
            OverlapPatch.ServerEndTime = GetWorld()->GetTimeSeconds() + Duration;
            OverlapPatch.TotalDuration = Duration;
            return true;
        }
    }

    // 4. Tworzymy nową plamę na powierzchni
    FSurfaceStatusPatch NewPatch;
    NewPatch.PatchID = NextPatchID++;
    NewPatch.StatusType = NewStatus;
    NewPatch.ImpactPoint = ImpactPoint;
    NewPatch.ImpactNormal = ImpactNormal;
    NewPatch.Radius = Radius;
    NewPatch.ServerEndTime = GetWorld()->GetTimeSeconds() + Duration;
    NewPatch.TotalDuration = Duration;

    ActivePatches.Add(NewPatch);
    UpdateTickState();
    OnSurfacePatchCreated.Broadcast(NewPatch);

    return true;
}

bool USurfaceStatusComponent::RemovePatch(int32 PatchID)
{
    REQUIRE_AUTHORITY_RET(false);

    for (int32 i = 0; i < ActivePatches.Num(); ++i)
    {
        if (ActivePatches[i].PatchID == PatchID)
        {
            const FSurfaceStatusPatch Removed = ActivePatches[i];
            ActivePatches.RemoveAt(i);
            UpdateTickState();
            OnSurfacePatchRemoved.Broadcast(Removed);
            return true;
        }
    }
    return false;
}

void USurfaceStatusComponent::ClearAllPatches()
{
    REQUIRE_AUTHORITY();

    if (ActivePatches.Num() == 0)
    {
        return;
    }

    const TArray<FSurfaceStatusPatch> OldPatches = ActivePatches;
    ActivePatches.Empty();
    UpdateTickState();

    for (const FSurfaceStatusPatch& P : OldPatches)
    {
        OnSurfacePatchRemoved.Broadcast(P);
    }
}

bool USurfaceStatusComponent::HasStatusAtLocation(EStatusEffectType Status, const FVector& Location, const FVector& Normal, float Tolerance) const
{
    const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    for (const FSurfaceStatusPatch& Patch : ActivePatches)
    {
        if (Patch.StatusType == Status && CurrentTime < Patch.ServerEndTime)
        {
            const float Dist = FVector::Dist(Patch.ImpactPoint, Location);
            const float Dot = FVector::DotProduct(Patch.ImpactNormal, Normal);
            if (Dist <= (Patch.Radius + Tolerance) && Dot > 0.5f)
            {
                return true;
            }
        }
    }
    return false;
}

void USurfaceStatusComponent::OnRep_ActivePatches(const TArray<FSurfaceStatusPatch>& OldPatches)
{
    UpdateTickState();

    for (const FSurfaceStatusPatch& NewPatch : ActivePatches)
    {
        if (!OldPatches.Contains(NewPatch))
        {
            OnSurfacePatchCreated.Broadcast(NewPatch);
        }
    }

    for (const FSurfaceStatusPatch& OldPatch : OldPatches)
    {
        if (!ActivePatches.Contains(OldPatch))
        {
            OnSurfacePatchRemoved.Broadcast(OldPatch);
        }
    }
}

void USurfaceStatusComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!GetWorld())
    {
        return;
    }

    const float CurrentTime = GetWorld()->GetTimeSeconds();

    // 1. Logika autorytatywna serwera (DoT i wygasanie)
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        // Okresowe obrażenia od ognia (DoT)
        bool bHasBurning = false;
        for (const FSurfaceStatusPatch& Patch : ActivePatches)
        {
            if (Patch.StatusType == EStatusEffectType::Burning)
            {
                bHasBurning = true;
                break;
            }
        }

        if (bHasBurning && (CurrentTime - LastBurnTickTime) >= 1.0f)
        {
            LastBurnTickTime = CurrentTime;
            if (CachedDamageableComp.IsValid())
            {
                CachedDamageableComp->ApplyDamage(BurnDamagePerSecond);
            }
        }

        // Wygasanie plam
        for (int32 i = ActivePatches.Num() - 1; i >= 0; --i)
        {
            if (CurrentTime >= ActivePatches[i].ServerEndTime)
            {
                const FSurfaceStatusPatch Expired = ActivePatches[i];
                ActivePatches.RemoveAt(i);
                OnSurfacePatchRemoved.Broadcast(Expired);
            }
        }

        if (ActivePatches.Num() == 0)
        {
            UpdateTickState();
        }
    }

    // 2. Precyzyjne renderowanie debug 3D na powierzchni (klient i serwer)
    DrawDebugVisuals();
}

void USurfaceStatusComponent::DrawDebugVisuals() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (!bShowDebugInWorld || !GetWorld())
    {
        return;
    }

    const float CurrentTime = GetWorld()->GetTimeSeconds();

    for (const FSurfaceStatusPatch& Patch : ActivePatches)
    {
        const float Remaining = FMath::Max(0.0f, Patch.ServerEndTime - CurrentTime);

        FColor StatusColor = FColor::White;
        FString StatusName = TEXT("STATUS");
        switch (Patch.StatusType)
        {
        case EStatusEffectType::Burning:
            StatusColor = FColor(255, 60, 0);
            StatusName = TEXT("BURNING");
            break;
        case EStatusEffectType::Wet:
            StatusColor = FColor(0, 180, 255);
            StatusName = TEXT("WET");
            break;
        case EStatusEffectType::Electrified:
            StatusColor = FColor(255, 230, 0);
            StatusName = TEXT("ELECTRIFIED");
            break;
        case EStatusEffectType::Oiled:
            StatusColor = FColor(180, 110, 40);
            StatusName = TEXT("OILED");
            break;
        default:
            break;
        }

        // Rysujemy okrąg plamy idealnie na płaszczyźnie ściany/filaru
        const FVector PatchCenter = Patch.ImpactPoint + (FVector)Patch.ImpactNormal * 1.5f;
        FMatrix Matrix = FRotationMatrix::MakeFromZ(Patch.ImpactNormal);
        Matrix.SetOrigin(PatchCenter);
        DrawDebugCircle(GetWorld(), Matrix, Patch.Radius, 32, StatusColor, false, 0.0f, 0, 2.5f, false);

        // Precyzyjny napis z odliczaniem czasu, odsunięty o 12 cm w stronę pokoju
        const FVector TextPos = PatchCenter + (FVector)Patch.ImpactNormal * 12.0f;
        DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("%s (%.1fs)"), *StatusName, Remaining), nullptr, StatusColor, 0.0f, true, 1.2f);
    }
#endif
}

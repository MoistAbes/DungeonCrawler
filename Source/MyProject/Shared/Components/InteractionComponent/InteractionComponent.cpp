#include "InteractionComponent.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Interfaces/InteractableInterface.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsHolding || !CurrentInteractingActor.IsValid())
    {
        StopHoldingInteraction();
        return;
    }

    CurrentHoldTime += DeltaTime;
    const float Progress = CurrentHoldDuration > 0.0f ? FMath::Clamp(CurrentHoldTime / CurrentHoldDuration, 0.0f, 1.0f) : 1.0f;
    OnInteractionProgressChanged.Broadcast(Progress);

    if (Progress >= 1.0f)
    {
        AActor* CompletedActor = CurrentInteractingActor.Get();
        StopHoldingInteraction();

        if (CompletedActor)
        {
            if (NetUtils::HasAuthority(this))
            {
                const AActor* Owner = GetOwner();
                if (IsValid(Owner) &&
                    NetUtils::ValidateInteractionDistance(Owner, CompletedActor, TraceDistance, 100.0f) &&
                    NetUtils::ValidateLineOfSight(Owner, CompletedActor, InteractionChannel))
                {
                    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(CompletedActor))
                    {
                        Interactable->Interact(GetOwner());
                    }
                    OnInteractionCompleted.Broadcast(CompletedActor);
                }
            }
            else
            {
                Server_RequestInteract(CompletedActor);
                OnInteractionCompleted.Broadcast(CompletedActor);
            }
        }
    }
}

void UInteractionComponent::GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return;

    if (const APawn* PawnOwner = Cast<APawn>(Owner))
    {
        if (const APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController()))
        {
            if (PC->PlayerCameraManager)
            {
                OutLocation = PC->PlayerCameraManager->GetCameraLocation();
                OutRotation = PC->PlayerCameraManager->GetCameraRotation();
                return;
            }
        }
    }

    Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
}

bool UInteractionComponent::PerformTrace(FHitResult& OutHit) const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return false;

    const UWorld* World = GetWorld();
    if (!World) return false;

    // Promień ZAWSZE wychodzi z oczu postaci - eliminuje sięganie kamerą 3rd person zza rogów/ścian
    const FVector Start = Owner->IsA<APawn>()
        ? Cast<APawn>(Owner)->GetPawnViewLocation()
        : Owner->GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);

    FVector CameraLoc;
    FRotator CameraRot;
    GetCameraViewPoint(CameraLoc, CameraRot);

    // Długość promienia to dokładnie maksymalny zasięg interakcji (300 cm)
    const FVector End = Start + (CameraRot.Vector() * TraceDistance);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionTrace), false, Owner);
    Params.AddIgnoredActor(Owner);

    bool bHit = false;
    if (InteractionTraceRadius > 0.0f)
    {
        const FCollisionShape SphereShape = FCollisionShape::MakeSphere(InteractionTraceRadius);
        bHit = World->SweepSingleByChannel(
            OutHit,
            Start,
            End,
            FQuat::Identity,
            InteractionChannel,
            SphereShape,
            Params
        );
    }
    else
    {
        bHit = World->LineTraceSingleByChannel(
            OutHit,
            Start,
            End,
            InteractionChannel,
            Params
        );
    }

#if ENABLE_DRAW_DEBUG
    if (bHit)
    {
        DrawDebugSphere(World, OutHit.ImpactPoint, InteractionTraceRadius, 12, FColor::Green, false, 2.0f, 0, 1.5f);
    }
#endif

    return bHit;
}

void UInteractionComponent::InteractWith(AActor* TargetActor)
{
    if (!TargetActor) return;

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastClientInteractionTime < MinInteractionInterval)
    {
        return;
    }

    IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor);
    if (!Interactable || !Interactable->CanInteract(GetOwner()))
    {
        return;
    }

    LastClientInteractionTime = CurrentTime;

    if (NetUtils::HasAuthority(this))
    {
        const AActor* Owner = GetOwner();
        if (!IsValid(Owner) ||
            !NetUtils::ValidateInteractionDistance(Owner, TargetActor, TraceDistance, 100.0f) ||
            !NetUtils::ValidateLineOfSight(Owner, TargetActor, InteractionChannel))
        {
            return;
        }

        Interactable->Interact(GetOwner());
        OnInteractionCompleted.Broadcast(TargetActor);
    }
    else
    {
        Server_RequestInteract(TargetActor);
    }
}

void UInteractionComponent::TriggerInstantInteraction()
{
    FHitResult HitResult;
    if (PerformTrace(HitResult))
    {
        InteractWith(HitResult.GetActor());
    }
}

void UInteractionComponent::StartHoldingInteraction()
{
    FHitResult HitResult;
    if (!PerformTrace(HitResult))
    {
        return;
    }

    AActor* TargetActor = HitResult.GetActor();
    if (!TargetActor) return;

    IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor);
    if (!Interactable || !Interactable->CanInteract(GetOwner()))
    {
        return;
    }

    if (DefaultHoldDuration <= 0.0f)
    {
        // Jeśli czas trzymania wynosi 0, to natychmiastowa interakcja
        InteractWith(TargetActor);
        return;
    }

    bIsHolding = true;
    CurrentHoldTime = 0.0f;
    CurrentHoldDuration = DefaultHoldDuration;
    CurrentInteractingActor = TargetActor;
    SetComponentTickEnabled(true);
}

void UInteractionComponent::StopHoldingInteraction()
{
    if (bIsHolding)
    {
        bIsHolding = false;
        CurrentHoldTime = 0.0f;
        CurrentHoldDuration = 0.0f;
        CurrentInteractingActor = nullptr;
        SetComponentTickEnabled(false);
        OnInteractionProgressChanged.Broadcast(0.0f);
    }
}

// -------------------------------------------------------------------------------------------------
// RPC Implementations (Server)
// -------------------------------------------------------------------------------------------------

bool UInteractionComponent::Server_RequestInteract_Validate(AActor* TargetActor)
{
    return TargetActor != nullptr;
}

void UInteractionComponent::Server_RequestInteract_Implementation(AActor* TargetActor)
{
    if (!TargetActor) return;

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastServerInteractionTime < MinInteractionInterval)
    {
        return;
    }
    LastServerInteractionTime = CurrentTime;

    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    // 1. Walidacja odległości przestrzennej (Distance Check z tolerancją)
    if (!NetUtils::ValidateInteractionDistance(Owner, TargetActor, TraceDistance, 50.0f))
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[InteractionComponent][Server] Server_RequestInteract Denied: Target %s out of range for %s."),
            *GetNameSafe(TargetActor), *GetNameSafe(Owner));
        return;
    }

    // 2. Walidacja widoczności (Line of Sight Check)
    if (!NetUtils::ValidateLineOfSight(Owner, TargetActor, InteractionChannel))
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[InteractionComponent][Server] Server_RequestInteract Denied: No line of sight to %s from %s."),
            *GetNameSafe(TargetActor), *GetNameSafe(Owner));
        return;
    }

    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
    {
        if (Interactable->CanInteract(Owner))
        {
            Interactable->Interact(Owner);
            OnInteractionCompleted.Broadcast(TargetActor);
        }
    }
}

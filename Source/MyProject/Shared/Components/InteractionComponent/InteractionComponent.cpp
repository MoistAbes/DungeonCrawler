#include "InteractionComponent.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
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

void UInteractionComponent::PrimaryInteract()
{
    UE_LOG(LogTemp, Warning, TEXT("[InteractionService]%s PrimaryInteract triggered."), *NetUtils::GetNetRolePrefix(this));

    if (GrabbedActor)
    {
        if (NetUtils::HasAuthority(this))
        {
            ExecuteRelease(false, FVector::ZeroVector);
        }
        else
        {
            Server_RequestReleaseOrThrow(false, FVector_NetQuantize::ZeroVector);
            ExecuteRelease(false, FVector::ZeroVector);
        }
        return;
    }

    FHitResult HitResult;
    if (!PerformTrace(HitResult))
    {
        return;
    }

    AActor* HitActor = HitResult.GetActor();
    if (!HitActor) return;

    // 1. Priorytet Fizyczny: Sprawdź czy obiekt można chwycić (IGrabbable)
    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(HitActor))
    {
        if (Grabbable->CanGrab(GetOwner()))
        {
            if (Grabbable->GetMass() > MaxCarryMass)
            {
                UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Prop mass (%.1f kg) exceeds limit (%.1f kg)."), 
                    Grabbable->GetMass(), MaxCarryMass);
                return;
            }

            if (NetUtils::HasAuthority(this))
            {
                ExecuteGrab(HitActor, HitResult.GetComponent());
            }
            else
            {
                Server_RequestGrab(HitActor, HitResult.GetComponent());
                ExecuteGrab(HitActor, HitResult.GetComponent());
            }
            return;
        }
    }

    // 2. Priorytet Logiczny: Standardowa akcja logiczna (IInteractable) - np. ASimpleSwitchProp
    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor))
    {
        if (Interactable->CanInteract(GetOwner()))
        {
            if (NetUtils::HasAuthority(this))
            {
                Interactable->Interact(GetOwner());
            }
            else
            {
                Server_RequestInteract(HitActor);
            }
            return;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Actor %s has no actionable contract."), *HitActor->GetName());
}

void UInteractionComponent::ThrowCurrentProp()
{
    if (!GrabbedActor) return;

    FVector CameraLoc;
    FRotator CameraRot;
    GetCameraViewPoint(CameraLoc, CameraRot);

    const FVector LaunchVelocity = CameraRot.Vector() * ThrowImpulseStrength;

    if (NetUtils::HasAuthority(this))
    {
        ExecuteRelease(true, LaunchVelocity);
    }
    else
    {
        Server_RequestReleaseOrThrow(true, FVector_NetQuantize(LaunchVelocity));
        ExecuteRelease(true, LaunchVelocity);
    }
}

bool UInteractionComponent::PerformTrace(FHitResult& OutHit) const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return false;

    FVector CameraLocation;
    FRotator CameraRotation;
    GetCameraViewPoint(CameraLocation, CameraRotation);

    const float ExtendedTraceDistance = TraceDistance + 1000.0f;
    const FVector TraceEnd = CameraLocation + (CameraRotation.Vector() * ExtendedTraceDistance);

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Owner);

    const bool bHit = GetWorld()->LineTraceSingleByChannel(
        OutHit,
        CameraLocation,
        TraceEnd,
        ECC_Visibility,
        Params
    );

    if (!bHit)
    {
        return false;
    }

    const float DistanceFromPlayer = FVector::Dist(OutHit.ImpactPoint, Owner->GetActorLocation());
    if (DistanceFromPlayer > TraceDistance)
    {
        return false;
    }

    DrawDebugLine(GetWorld(), CameraLocation, OutHit.ImpactPoint, FColor::Green, false, 2.0f, 0, 2.0f);
    return true;
}

void UInteractionComponent::ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor || !ComponentToGrab) return;

    GrabbedActor = TargetActor;
    GrabbedComponent = ComponentToGrab;

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(GrabbedActor))
    {
        Grabbable->OnGrabbed(GetOwner());
    }

    // Podpinamy obiekt do postaci (wspólna metoda z biblioteki domenowej Networking)
    NetUtils::AttachCarriedProp(GrabbedActor, GrabbedComponent, GetOwner());

    UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Grabbed & Attached: %s"), *NetUtils::GetNetRolePrefix(this), *GrabbedActor->GetName());
}

void UInteractionComponent::ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity)
{
    if (!GrabbedActor) return;

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(GrabbedActor))
    {
        Grabbable->OnDropped(GetOwner());
    }

    // Odpinamy i włączamy fizykę z powrotem
    NetUtils::DetachCarriedProp(GrabbedActor, GrabbedComponent, GetOwner(), bIsThrow ? LaunchVelocity : FVector::ZeroVector);

    UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Released/Thrown: %s"), *NetUtils::GetNetRolePrefix(this), *GrabbedActor->GetName());

    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
}

// -------------------------------------------------------------------------------------------------
// RPC Implementations (Server)
// -------------------------------------------------------------------------------------------------

bool UInteractionComponent::Server_RequestGrab_Validate(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    return TargetActor != nullptr;
}

void UInteractionComponent::Server_RequestGrab_Implementation(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor || !ComponentToGrab) return;

    if (const AActor* Owner = GetOwner())
    {
        const float Dist = FVector::Dist(Owner->GetActorLocation(), TargetActor->GetActorLocation());
        if (Dist > (TraceDistance + 150.0f))
        {
            UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] Denied Grab: Target is too far (%.1f cm)"), Dist);
            return;
        }
    }

    ExecuteGrab(TargetActor, ComponentToGrab);
}

bool UInteractionComponent::Server_RequestReleaseOrThrow_Validate(bool bIsThrow, const FVector_NetQuantize& LaunchVelocity)
{
    return true;
}

void UInteractionComponent::Server_RequestReleaseOrThrow_Implementation(bool bIsThrow, const FVector_NetQuantize& LaunchVelocity)
{
    ExecuteRelease(bIsThrow, LaunchVelocity);
}

bool UInteractionComponent::Server_RequestInteract_Validate(AActor* TargetActor)
{
    return TargetActor != nullptr;
}

void UInteractionComponent::Server_RequestInteract_Implementation(AActor* TargetActor)
{
    if (!TargetActor) return;

    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
    {
        if (Interactable->CanInteract(GetOwner()))
        {
            Interactable->Interact(GetOwner());
        }
    }
}

#include "InteractionComponent.h"

#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        PhysicsHandle = Owner->FindComponentByClass<UPhysicsHandleComponent>();
        if (!PhysicsHandle)
        {
            UE_LOG(LogTemp, Error, TEXT("[InteractionService] PhysicsHandleComponent NOT FOUND on Actor: %s!"), *Owner->GetName());
        }
    }
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (GrabbedActor && PhysicsHandle)
    {
        UpdateHoldLocation();
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

void UInteractionComponent::PrimaryInteract()
{
    UE_LOG(LogTemp, Warning, TEXT("[InteractionService] PrimaryInteract triggered."));

    if (GrabbedActor)
    {
        ReleaseProp();
        return;
    }

    FHitResult HitResult;
    if (!PerformTrace(HitResult))
    {
        return;
    }

    AActor* HitActor = HitResult.GetActor();
    if (!HitActor) return;

    // 1. Priorytet Fizyczny: Sprawdź czy obiekt można fizycznie chwycić (IGrabbable)
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

            GrabProp(HitActor, HitResult.GetComponent());
            return;
        }
    }

    // 2. Priorytet Logiczny: Standardowa akcja logiczna (IInteractable) - np. ASimpleSwitchProp
    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor))
    {
        if (Interactable->CanInteract(GetOwner()))
        {
            Interactable->Interact(GetOwner());
            return;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Actor %s has no actionable contract."), *HitActor->GetName());
}

bool UInteractionComponent::PerformTrace(FHitResult& OutHit) const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return false;

    FVector CameraLocation;
    FRotator CameraRotation;
    GetCameraViewPoint(CameraLocation, CameraRotation);

    // Celowanie oparte na widoku kamery / wzroku postaci
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

    // Weryfikacja zasięgu: czy trafiony punkt znajduje się w dopuszczalnym zasięgu postaci
    const float DistanceFromPlayer = FVector::Dist(OutHit.ImpactPoint, Owner->GetActorLocation());
    if (DistanceFromPlayer > TraceDistance)
    {
        return false;
    }

    DrawDebugLine(GetWorld(), CameraLocation, OutHit.ImpactPoint, FColor::Green, false, 2.0f, 0, 2.0f);
    return true;
}

void UInteractionComponent::GrabProp(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor || !ComponentToGrab || !PhysicsHandle) return;

    GrabbedActor = TargetActor;
    GrabbedComponent = ComponentToGrab;

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(GrabbedActor))
    {
        Grabbable->OnGrabbed(GetOwner());
    }

    if (AActor* OwnerActor = GetOwner())
    {
        GrabbedComponent->IgnoreActorWhenMoving(OwnerActor, true);
    }

    GrabbedComponent->SetNotifyRigidBodyCollision(true);
    GrabbedComponent->WakeRigidBody();

    PhysicsHandle->GrabComponentAtLocationWithRotation(
        GrabbedComponent,
        NAME_None,
        GrabbedComponent->GetComponentLocation(),
        GrabbedComponent->GetComponentRotation()
    );

    SetComponentTickEnabled(true);
    UE_LOG(LogTemp, Log, TEXT("[InteractionService] Successfully grabbed: %s"), *GrabbedActor->GetName());
}

void UInteractionComponent::ReleaseProp()
{
    if (!GrabbedActor || !PhysicsHandle) return;

    SetComponentTickEnabled(false);
    PhysicsHandle->ReleaseComponent();

    if (GrabbedComponent)
    {
        GrabbedComponent->SetNotifyRigidBodyCollision(true);
        GrabbedComponent->WakeRigidBody();

        if (AActor* OwnerActor = GetOwner())
        {
            TWeakObjectPtr<UPrimitiveComponent> WeakComp = GrabbedComponent;
            TWeakObjectPtr<AActor> WeakOwner = OwnerActor;

            // Przywracamy kolizję z graczem z bezpiecznym buforem czasowym (zapobiega glitchom i uderzeniom o własną kapsułę)
            if (UWorld* World = GetWorld())
            {
                FTimerHandle UnignoreTimer;
                World->GetTimerManager().SetTimer(UnignoreTimer, [WeakComp, WeakOwner]()
                {
                    if (WeakComp.IsValid() && WeakOwner.IsValid())
                    {
                        WeakComp->IgnoreActorWhenMoving(WeakOwner.Get(), false);
                    }
                }, 0.25f, false);
            }
        }
    }

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(GrabbedActor))
    {
        Grabbable->OnDropped(GetOwner());
    }

    UE_LOG(LogTemp, Log, TEXT("[InteractionService] Released: %s"), *GrabbedActor->GetName());

    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
}

void UInteractionComponent::ThrowCurrentProp()
{
    if (!GrabbedActor || !GrabbedComponent) return;

    UPrimitiveComponent* PropPhysicsComp = GrabbedComponent;
    AActor* Owner = GetOwner();

    FVector CameraLoc;
    FRotator CameraRot;
    GetCameraViewPoint(CameraLoc, CameraRot);

    ReleaseProp();

    if (PropPhysicsComp && Owner)
    {
        PropPhysicsComp->SetNotifyRigidBodyCollision(true);
        PropPhysicsComp->WakeRigidBody();

        const FVector LaunchVelocity = CameraRot.Vector() * ThrowImpulseStrength;
        PropPhysicsComp->AddImpulse(LaunchVelocity, NAME_None, true);

        UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Prop launched with Velocity: %s"), *LaunchVelocity.ToString());
    }
}

void UInteractionComponent::UpdateHoldLocation()
{
    const AActor* Owner = GetOwner();
    if (!Owner || !PhysicsHandle) return;

    FVector CameraLoc;
    FRotator CameraRot;
    GetCameraViewPoint(CameraLoc, CameraRot);

    // Punkt zawieszenia trzymanego obiektu w osi kamery przed postacią
    const FVector PlayerCenter = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
    const FVector TargetLocation = PlayerCenter + (CameraRot.Vector() * HoldDistance);

    PhysicsHandle->SetTargetLocationAndRotation(TargetLocation, CameraRot);
}

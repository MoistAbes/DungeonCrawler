#include "InteractionComponent.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Player/PlayerCharacter.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsValid(GrabbedActor))
    {
        GrabbedActor = nullptr;
        GrabbedComponent = nullptr;
        SetComponentTickEnabled(false);
        return;
    }

    UpdateHoldAnchorTransform(DeltaTime);
    UpdateCarriedPropTransform(DeltaTime);
}

void UInteractionComponent::NotifyCarriedPropAttached(AActor* InProp)
{
    GrabbedActor = InProp;
    SetComponentTickEnabled(true);
}

void UInteractionComponent::NotifyCarriedPropDetached()
{
    if (IsValid(GrabbedActor))
    {
        GrabbedActor->SetReplicateMovement(true);
    }
    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
    SetComponentTickEnabled(false);
}

void UInteractionComponent::UpdateHoldAnchorTransform(float DeltaTime)
{
    APawn* PawnOwner = Cast<APawn>(GetOwner());
    if (!PawnOwner) return;

    APlayerCharacter* PlayerChar = Cast<APlayerCharacter>(PawnOwner);
    if (!PlayerChar || !PlayerChar->HoldAnchorComponent) return;

    // 1. Pobieramy kąt patrzenia (Aim Pitch)
    float AimPitch = 0.0f;
    if (PawnOwner->IsLocallyControlled())
    {
        if (const APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController()))
        {
            if (PC->PlayerCameraManager)
            {
                AimPitch = PC->PlayerCameraManager->GetCameraRotation().Pitch;
            }
            else
            {
                AimPitch = PC->GetControlRotation().Pitch;
            }
        }
        else
        {
            AimPitch = PawnOwner->GetControlRotation().Pitch;
        }
    }
    else
    {
        AimPitch = PawnOwner->GetBaseAimRotation().Pitch;
    }
    AimPitch = FRotator::NormalizeAxis(AimPitch);

    // Ograniczamy kąt w pionie (od -50 st w dół do +50 st w górę)
    const float ClampedPitch = FMath::Clamp(AimPitch, -50.0f, 50.0f);
    const float PitchRad = FMath::DegreesToRadians(ClampedPitch);

    // 2. Pozycja relatywna kotwicy względem kapsuły postaci
    const float BaseZ = PlayerChar->BaseEyeHeightOffset - 15.0f;
    const float HoldDistance = 110.0f;

    const float TargetX = HoldDistance * FMath::Cos(PitchRad);
    const float TargetZ = BaseZ + (HoldDistance * FMath::Sin(PitchRad));

    // 3. Płynna interpolacja pozycji kotwicy
    const FVector CurrentRelLoc = PlayerChar->HoldAnchorComponent->GetRelativeLocation();
    const FVector TargetRelLoc(TargetX, 0.0f, TargetZ);
    const FVector NewRelLoc = FMath::VInterpTo(CurrentRelLoc, TargetRelLoc, DeltaTime, 20.0f);

    PlayerChar->HoldAnchorComponent->SetRelativeLocation(NewRelLoc);
}

void UInteractionComponent::UpdateCarriedPropTransform(float DeltaTime)
{
    if (!IsValid(GrabbedActor)) return;

    APlayerCharacter* PlayerChar = Cast<APlayerCharacter>(GetOwner());
    if (!PlayerChar || !PlayerChar->HoldAnchorComponent) return;

    const FVector TargetLocation = PlayerChar->HoldAnchorComponent->GetComponentLocation();
    const FRotator TargetRotation = PlayerChar->HoldAnchorComponent->GetComponentRotation();

    // Płynna interpolacja do punktu docelowego (daje naturalne wrażenie masy i płynność ruchu)
    const FVector CurrentLocation = GrabbedActor->GetActorLocation();
    const FRotator CurrentRotation = GrabbedActor->GetActorRotation();

    const FVector DesiredLocation = FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, 25.0f);
    const FRotator DesiredRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, 25.0f);

    // 1. Kinematyczny Sweep z testem kolizji
    FHitResult SweepHit;
    GrabbedActor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation, true, &SweepHit, ETeleportType::None);

    // 2. Inteligentna reakcja na napotkane przeszkody fizyczne
    if (SweepHit.bBlockingHit)
    {
        if (UPrimitiveComponent* HitComp = SweepHit.GetComponent())
        {
            if (HitComp->IsSimulatingPhysics())
            {
                const float HitMass = HitComp->GetMass();
                const float PreHitSpeed = HitComp->GetPhysicsLinearVelocity().Size();

                if (HitMass <= PlayerChar->MaxPushableMass)
                {
                    // Obiekt mieści się w limicie udźwigu gracza (np. skrzynia 60 kg) - przekazujemy fizyczną siłę pchania
                    FVector PushDir = -SweepHit.ImpactNormal;
                    PushDir.Z = 0.0f;
                    PushDir = PushDir.GetSafeNormal();

                    if (PushDir.IsNearlyZero())
                    {
                        PushDir = PlayerChar->GetActorForwardVector();
                    }

                    HitComp->WakeRigidBody();
                    HitComp->AddForceAtLocation(PushDir * PlayerChar->PlayerPushForce, SweepHit.ImpactPoint, SweepHit.BoneName);
                }
                else
                {
                    // Obiekt jest zbyt ciężki dla gracza (np. beczka 177-200 kg).
                    // Jeśli był w spoczynku / powolnym ruchu, uniemożliwiamy szturnięcia i spychacz,
                    // zerując wszelkie sztuczne impulsy kontaktowe solvera Chaos.
                    if (PreHitSpeed < 60.0f)
                    {
                        HitComp->SetPhysicsLinearVelocity(FVector::ZeroVector);
                        HitComp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
                    }
                }
            }
        }
    }

    // 3. Zabezpieczenie przed rotacją kamery i szturnięciami od boku:
    // Sprawdzamy komponenty nakładające się na trzymany prop. Jeśli któryś jest zbyt ciężki i w spoczynku - blokujemy pchnięcie.
    if (UPrimitiveComponent* PropPrim = GrabbedComponent ? GrabbedComponent.Get() : Cast<UPrimitiveComponent>(GrabbedActor->GetRootComponent()))
    {
        TArray<UPrimitiveComponent*> Overlaps;
        PropPrim->GetOverlappingComponents(Overlaps);
        for (UPrimitiveComponent* OverlapComp : Overlaps)
        {
            if (OverlapComp && OverlapComp->IsSimulatingPhysics())
            {
                if (OverlapComp->GetMass() > PlayerChar->MaxPushableMass)
                {
                    if (OverlapComp->GetPhysicsLinearVelocity().Size() < 60.0f)
                    {
                        OverlapComp->SetPhysicsLinearVelocity(FVector::ZeroVector);
                        OverlapComp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
                    }
                }
            }
        }
    }

    // 4. Weryfikacja dystansu: czy ręce gracza nie zostały zbyt mocno oddalone od zablokowanego propa
    const float DistanceFromHands = FVector::Dist(GrabbedActor->GetActorLocation(), TargetLocation);

    if (DistanceFromHands > CarryBreakDistance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService]%s Carry grip broken! Distance (%.1f cm) exceeded limit (%.1f cm)."),
            *NetUtils::GetNetRolePrefix(this), DistanceFromHands, CarryBreakDistance);

        // Zerwanie chwytu – upuszczenie przedmiotu
        PrimaryInteract();
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
    UE_LOG(LogTemp, Warning, TEXT("[InteractionService]%s PrimaryInteract triggered."), *NetUtils::GetNetRolePrefix(this));

    // Jeśli trzymamy obiekt - upuszczamy go
    if (GrabbedActor)
    {
        if (NetUtils::HasAuthority(this))
        {
            ExecuteRelease(false, FVector::ZeroVector);
        }
        else
        {
            Server_RequestReleaseOrThrow(false, FVector_NetQuantize::ZeroVector);
            GrabbedActor = nullptr;
            GrabbedComponent = nullptr;
            SetComponentTickEnabled(false);
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
                GrabbedActor = HitActor;
                GrabbedComponent = HitResult.GetComponent();
                SetComponentTickEnabled(true);
                Server_RequestGrab(HitActor, HitResult.GetComponent());
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
        GrabbedActor = nullptr;
        GrabbedComponent = nullptr;
        SetComponentTickEnabled(false);
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

    SetComponentTickEnabled(true);

    UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Grabbed: %s"), *NetUtils::GetNetRolePrefix(this), *GetNameSafe(GrabbedActor));
}

void UInteractionComponent::ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity)
{
    if (!GrabbedActor) return;

    AActor* ReleasedActor = GrabbedActor;
    const FVector AppliedVelocity = bIsThrow ? LaunchVelocity : FVector::ZeroVector;

    // Reset stanu lokalnego komponentu
    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
    SetComponentTickEnabled(false);

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(ReleasedActor))
    {
        Grabbable->OnDropped(GetOwner(), AppliedVelocity);
    }

    UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Released/Thrown: %s (Velocity: %s)"), 
        *NetUtils::GetNetRolePrefix(this), *GetNameSafe(ReleasedActor), *AppliedVelocity.ToString());
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

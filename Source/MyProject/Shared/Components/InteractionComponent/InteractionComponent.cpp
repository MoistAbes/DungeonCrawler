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

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsValid(GrabbedActor))
    {
        ResetGrabState();
        return;
    }

    UpdateHoldAnchorTransform(DeltaTime);
    UpdateCarriedPropTransform(DeltaTime);
}

void UInteractionComponent::NotifyCarriedPropAttached(AActor* InProp)
{
    GrabbedActor = InProp;
    FVector CamLoc;
    FRotator CamRot;
    GetCameraViewPoint(CamLoc, CamRot);
    PreviousCameraRotation = CamRot;
    TrackedCameraSwingVelocity = FVector::ZeroVector;
    SetComponentTickEnabled(true);
}

void UInteractionComponent::NotifyCarriedPropDetached()
{
    if (IsValid(GrabbedActor))
    {
        GrabbedActor->SetReplicateMovement(true);
    }
    ResetGrabState();
}

void UInteractionComponent::ResetGrabState()
{
    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
    PreviousCameraRotation = FRotator::ZeroRotator;
    TrackedCameraSwingVelocity = FVector::ZeroVector;
    SetComponentTickEnabled(false);
}

void UInteractionComponent::StopHeavyPhysicsObject(UPrimitiveComponent* Comp, float MaxPushableMass)
{
    if (!Comp || !Comp->IsSimulatingPhysics())
    {
        return;
    }

    if (Comp->GetMass() > MaxPushableMass)
    {
        // Zerujemy mikroruchy i sztuczne impulsy kontaktowe solvera Chaos dla obiektów ciężkich (np. beczki 177-200 kg)
        if (Comp->GetPhysicsLinearVelocity().Size() < 60.0f)
        {
            Comp->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Comp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
    }
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

    // 2. Śledzenie czystej prędkości kątowej kamery (zamach myszką w 100% niezależny od ruchu postaci)
    FVector CamLoc;
    FRotator CamRot;
    GetCameraViewPoint(CamLoc, CamRot);

    if (!PreviousCameraRotation.IsZero() && DeltaTime > 0.0001f)
    {
        const FRotator DeltaRot = (CamRot - PreviousCameraRotation).GetNormalized();
        const float YawRateRad = FMath::DegreesToRadians(DeltaRot.Yaw / DeltaTime);
        const float PitchRateRad = FMath::DegreesToRadians(DeltaRot.Pitch / DeltaTime);

        // Obliczamy prędkość liniową na ramieniu trzymania (ok. 110 cm)
        const float HoldRadius = 110.0f;
        const FVector TangentialVelocity = (CamRot.RotateVector(FVector::RightVector) * (YawRateRad * HoldRadius))
                                         + (CamRot.RotateVector(FVector::UpVector) * (PitchRateRad * HoldRadius));

        TrackedCameraSwingVelocity = FMath::VInterpTo(TrackedCameraSwingVelocity, TangentialVelocity, DeltaTime, 16.0f);
    }
    PreviousCameraRotation = CamRot;

    // 3. Reakcja na napotkane przeszkody fizyczne
    if (SweepHit.bBlockingHit)
    {
        if (UPrimitiveComponent* HitComp = SweepHit.GetComponent())
        {
            if (HitComp->IsSimulatingPhysics())
            {
                const float HitMass = HitComp->GetMass();
                if (HitMass <= PlayerChar->MaxPushableMass)
                {
                    // Obiekt mieści się w limicie udźwigu gracza - przekazujemy fizyczną siłę pchania
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
                    StopHeavyPhysicsObject(HitComp, PlayerChar->MaxPushableMass);
                }
            }
        }
    }

    // 4. Zabezpieczenie przed rotacją kamery i szturnięciami od boku (Overlaps check)
    if (UPrimitiveComponent* PropPrim = GrabbedComponent ? GrabbedComponent.Get() : Cast<UPrimitiveComponent>(GrabbedActor->GetRootComponent()))
    {
        TArray<UPrimitiveComponent*> Overlaps;
        PropPrim->GetOverlappingComponents(Overlaps);
        for (UPrimitiveComponent* OverlapComp : Overlaps)
        {
            StopHeavyPhysicsObject(OverlapComp, PlayerChar->MaxPushableMass);
        }
    }

    // 5. Weryfikacja dystansu: czy ręce gracza nie zostały zbyt mocno oddalone od zablokowanego propa
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

    // Jeśli trzymamy obiekt: czyste upuszczenie pod nogi (brak pędu) LUB rzut zamachem myszką
    if (GrabbedActor)
    {
        FVector ReleaseVelocity = FVector::ZeroVector;

        // Jeśli gracz wykonał celowy, dynamiczny zamach samą myszką (powyżej progu):
        const float SwingSpeed = TrackedCameraSwingVelocity.Size();
        if (SwingSpeed >= MinSwingSpeedToThrow)
        {
            const float ScaledSpeed = FMath::Clamp(SwingSpeed * SwingVelocityMultiplier, 0.0f, MaxSwingThrowSpeed);
            ReleaseVelocity = TrackedCameraSwingVelocity.GetSafeNormal() * ScaledSpeed;

            UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Mouse Swing Throw! Swing Speed: %.1f cm/s"),
                *NetUtils::GetNetRolePrefix(this), ScaledSpeed);
        }
        else
        {
            // Ruch postaci/bieg nie generuje pędu: prop po prostu opada pod stopy
            UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Pure drop under feet (Zero launch velocity)."),
                *NetUtils::GetNetRolePrefix(this));
        }

        const bool bIsThrow = !ReleaseVelocity.IsNearlyZero();

        if (NetUtils::HasAuthority(this))
        {
            ExecuteRelease(bIsThrow, ReleaseVelocity);
        }
        else
        {
            Server_RequestReleaseOrThrow(bIsThrow, FVector_NetQuantize(ReleaseVelocity));
            ResetGrabState();
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
        ResetGrabState();
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
    FVector CamLoc;
    FRotator CamRot;
    GetCameraViewPoint(CamLoc, CamRot);
    PreviousCameraRotation = CamRot;
    TrackedCameraSwingVelocity = FVector::ZeroVector;

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

    ResetGrabState();

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
            UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] Denied Grab: Target is too far (%.1f cm)"), Dist);\
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
    FVector ClampedVelocity = LaunchVelocity;
    if (ClampedVelocity.Size() > MaxSwingThrowSpeed)
    {
        ClampedVelocity = ClampedVelocity.GetSafeNormal() * MaxSwingThrowSpeed;
    }
    ExecuteRelease(bIsThrow, ClampedVelocity);
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

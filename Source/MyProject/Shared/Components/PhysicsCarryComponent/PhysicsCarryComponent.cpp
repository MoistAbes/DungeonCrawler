#include "PhysicsCarryComponent.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"

UPhysicsCarryComponent::UPhysicsCarryComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

void UPhysicsCarryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsValid(GrabbedActor))
    {
        ResetGrabState();
        return;
    }

    const APawn* PawnOwner = Cast<APawn>(GetOwner());
    const bool bIsLocalOwner = PawnOwner && PawnOwner->IsLocallyControlled();
    const bool bHasAuthority = NetUtils::HasAuthority(this);

    // Zdalni gracze (Remote Proxies) otrzymują ruch zreplikowany bezpośrednio z silnika
    if (!bHasAuthority && !bIsLocalOwner)
    {
        return;
    }

    UpdateHoldAnchorTransform(DeltaTime);
    UpdateCarriedPropTransform(DeltaTime);
}

void UPhysicsCarryComponent::NotifyCarriedPropAttached(AActor* InProp)
{
    GrabbedActor = InProp;
    if (InProp)
    {
        GrabbedComponent = Cast<UPrimitiveComponent>(InProp->GetRootComponent());
    }
    CarryState = ECarryState::Carrying;

    const APawn* PawnOwner = Cast<APawn>(GetOwner());
    const bool bIsLocalOwner = PawnOwner && PawnOwner->IsLocallyControlled();
    const bool bHasAuthority = NetUtils::HasAuthority(this);

    if (bHasAuthority || bIsLocalOwner)
    {
        FVector CamLoc;
        FRotator CamRot;
        GetCameraViewPoint(CamLoc, CamRot);
        PreviousCameraRotation = CamRot;
        TrackedCameraSwingVelocity = FVector::ZeroVector;
        SetComponentTickEnabled(true);

        if (GrabbedComponent)
        {
            BindPropOverlapEvents(GrabbedComponent.Get());
        }
    }
}

void UPhysicsCarryComponent::NotifyCarriedPropDetached()
{
    if (IsValid(GrabbedActor))
    {
        GrabbedActor->SetReplicateMovement(true);
    }
    ResetGrabState();
}

void UPhysicsCarryComponent::ResetGrabState()
{
    UnbindPropOverlapEvents();

    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
    CarryState = ECarryState::None;
    PreviousCameraRotation = FRotator::ZeroRotator;
    TrackedCameraSwingVelocity = FVector::ZeroVector;
    SetComponentTickEnabled(false);
}

void UPhysicsCarryComponent::BindPropOverlapEvents(UPrimitiveComponent* PropComp)
{
    UnbindPropOverlapEvents();

    if (!IsValid(PropComp)) return;

    PropComp->SetGenerateOverlapEvents(true);
    PropComp->OnComponentBeginOverlap.AddDynamic(this, &UPhysicsCarryComponent::OnPropBeginOverlap);
    PropComp->OnComponentEndOverlap.AddDynamic(this, &UPhysicsCarryComponent::OnPropEndOverlap);

    TArray<UPrimitiveComponent*> InitialOverlaps;
    PropComp->GetOverlappingComponents(InitialOverlaps);
    for (UPrimitiveComponent* OverlapComp : InitialOverlaps)
    {
        if (IsValid(OverlapComp) && OverlapComp->IsSimulatingPhysics() && OverlapComp->GetOwner() != GetOwner() && OverlapComp->GetOwner() != GrabbedActor)
        {
            OverlappingPhysicsComponents.AddUnique(OverlapComp);
        }
    }
}

void UPhysicsCarryComponent::UnbindPropOverlapEvents()
{
    if (GrabbedComponent)
    {
        GrabbedComponent->OnComponentBeginOverlap.RemoveDynamic(this, &UPhysicsCarryComponent::OnPropBeginOverlap);
        GrabbedComponent->OnComponentEndOverlap.RemoveDynamic(this, &UPhysicsCarryComponent::OnPropEndOverlap);
    }
    OverlappingPhysicsComponents.Reset();
}

void UPhysicsCarryComponent::OnPropBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!IsValid(OtherComp) || OtherActor == GetOwner() || OtherActor == GrabbedActor)
    {
        return;
    }

    if (OtherComp->IsSimulatingPhysics() || (OtherComp->GetOwner() && OtherComp->GetOwner()->IsRootComponentMovable()))
    {
        OverlappingPhysicsComponents.AddUnique(OtherComp);
    }
}

void UPhysicsCarryComponent::OnPropEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!IsValid(OtherComp))
    {
        return;
    }

    OverlappingPhysicsComponents.Remove(OtherComp);
}

void UPhysicsCarryComponent::StopHeavyPhysicsObject(UPrimitiveComponent* Comp, float CurrentMaxPushableMass)
{
    UKineticForceLibrary::SuppressHeavyPhysicsJitter(Comp, CurrentMaxPushableMass, VelocityStopThreshold);
}

FVector UPhysicsCarryComponent::CalculateHoldAnchorRelativeOffset(float AimPitch, float BaseEyeHeightOffset) const
{
    const float NormalizedPitch = FRotator::NormalizeAxis(AimPitch);
    const float ClampedPitch = FMath::Clamp(NormalizedPitch, CarrySocketConfig.MinPitch, CarrySocketConfig.MaxPitch);
    const float PitchRad = FMath::DegreesToRadians(ClampedPitch);

    const float HoldDistance = CarrySocketConfig.HoldDistance;
    const float BaseZ = BaseEyeHeightOffset + CarrySocketConfig.EyeHeightOffsetZ;
    const float TargetX = HoldDistance * FMath::Cos(PitchRad);
    const float TargetZ = BaseZ + (HoldDistance * FMath::Sin(PitchRad));

    return FVector(TargetX, 0.0f, TargetZ);
}

void UPhysicsCarryComponent::UpdateHoldAnchorTransform(float DeltaTime)
{
    APawn* PawnOwner = Cast<APawn>(GetOwner());
    if (!PawnOwner) return;

    ICarryAnchorProviderInterface* CarryProvider = Cast<ICarryAnchorProviderInterface>(PawnOwner);
    if (!CarryProvider) return;

    USceneComponent* HoldAnchor = CarryProvider->GetHoldAnchorComponent();
    if (!HoldAnchor) return;

    float AimPitch = 0.0f;
    if (PawnOwner->IsLocallyControlled())
    {
        if (const APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController()))
        {
            AimPitch = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Pitch : PC->GetControlRotation().Pitch;
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

    const FVector TargetRelLoc = CalculateHoldAnchorRelativeOffset(AimPitch, CarryProvider->GetCarryEyeHeightOffset());
    const FVector CurrentRelLoc = HoldAnchor->GetRelativeLocation();
    const FVector NewRelLoc = FMath::VInterpTo(CurrentRelLoc, TargetRelLoc, DeltaTime, CarrySocketConfig.AnchorInterpSpeed);

    HoldAnchor->SetRelativeLocation(NewRelLoc);
}

void UPhysicsCarryComponent::UpdateSwingVelocity(float DeltaTime)
{
    FVector CamLoc;
    FRotator CamRot;
    GetCameraViewPoint(CamLoc, CamRot);

    if (!PreviousCameraRotation.IsZero() && DeltaTime > 0.0001f)
    {
        const FRotator DeltaRot = (CamRot - PreviousCameraRotation).GetNormalized();
        const float YawRateRad = FMath::DegreesToRadians(DeltaRot.Yaw / DeltaTime);
        const float PitchRateRad = FMath::DegreesToRadians(DeltaRot.Pitch / DeltaTime);

        const float HoldRadius = CarrySocketConfig.HoldDistance;
        const FVector TangentialVelocity = (CamRot.RotateVector(FVector::RightVector) * (YawRateRad * HoldRadius))
                                         + (CamRot.RotateVector(FVector::UpVector) * (PitchRateRad * HoldRadius));

        TrackedCameraSwingVelocity = FMath::VInterpTo(TrackedCameraSwingVelocity, TangentialVelocity, DeltaTime, CarrySocketConfig.SwingInterpSpeed);
    }
    PreviousCameraRotation = CamRot;
}

void UPhysicsCarryComponent::HandleSweepCollision(const FHitResult& SweepHit, ICarryAnchorProviderInterface* CarryProvider, AActor* CarrierActor)
{
    if (!SweepHit.bBlockingHit || !CarryProvider || !CarrierActor) return;

    UKineticForceLibrary::TryApplyPhysicsPush(
        SweepHit.GetComponent(),
        SweepHit,
        CarrierActor->GetActorForwardVector(),
        CarryProvider->GetPlayerPushForce(),
        CarryProvider->GetMaxPushableMass(),
        VelocityStopThreshold);
}

void UPhysicsCarryComponent::SuppressOverlappingHeavyPhysics(ICarryAnchorProviderInterface* CarryProvider)
{
    if (!CarryProvider || OverlappingPhysicsComponents.IsEmpty()) return;

    const float MaxMass = CarryProvider->GetMaxPushableMass();
    for (int32 Index = OverlappingPhysicsComponents.Num() - 1; Index >= 0; --Index)
    {
        UPrimitiveComponent* Comp = OverlappingPhysicsComponents[Index].Get();
        if (!IsValid(Comp) || !Comp->IsSimulatingPhysics())
        {
            OverlappingPhysicsComponents.RemoveAtSwap(Index);
            continue;
        }

        StopHeavyPhysicsObject(Comp, MaxMass);
    }
}

bool UPhysicsCarryComponent::CheckGripBreakDistance(const FVector& TargetLocation)
{
    if (!IsValid(GrabbedActor)) return false;

    const float DistanceFromHands = FVector::Dist(GrabbedActor->GetActorLocation(), TargetLocation);
    if (DistanceFromHands > CarryBreakDistance)
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent]%s Carry grip broken! Distance (%.1f cm) exceeded limit (%.1f cm)."),
            *NetUtils::GetNetRolePrefix(this), DistanceFromHands, CarryBreakDistance);

        // Upuszczenie
        DropOrSwing();
        return true;
    }

    return false;
}

void UPhysicsCarryComponent::UpdateCarriedPropTransform(float DeltaTime)
{
    if (!IsValid(GrabbedActor)) return;

    AActor* OwnerActor = GetOwner();
    ICarryAnchorProviderInterface* CarryProvider = Cast<ICarryAnchorProviderInterface>(OwnerActor);
    if (!CarryProvider) return;

    USceneComponent* HoldAnchor = CarryProvider->GetHoldAnchorComponent();
    if (!HoldAnchor) return;

    const FVector TargetLocation = HoldAnchor->GetComponentLocation();
    const FRotator TargetRotation = HoldAnchor->GetComponentRotation();

    const FVector CurrentLocation = GrabbedActor->GetActorLocation();
    const FRotator CurrentRotation = GrabbedActor->GetActorRotation();

    const FVector DesiredLocation = FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, CarrySocketConfig.PropInterpSpeed);
    const FRotator DesiredRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, CarrySocketConfig.PropInterpSpeed);

    FHitResult SweepHit;
    GrabbedActor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation, true, &SweepHit, ETeleportType::None);

    UpdateSwingVelocity(DeltaTime);
    HandleSweepCollision(SweepHit, CarryProvider, OwnerActor);
    SuppressOverlappingHeavyPhysics(CarryProvider);
    CheckGripBreakDistance(TargetLocation);
}

void UPhysicsCarryComponent::GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const
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

FVector UPhysicsCarryComponent::CalculateServerThrowVelocity() const
{
    FVector CameraLoc;
    FRotator CameraRot;
    GetCameraViewPoint(CameraLoc, CameraRot);

    FVector ThrowDir = CameraRot.Vector();

    if (ThrowDir.Z > -0.2f && ThrowDir.Z < 0.2f)
    {
        ThrowDir.Z += 0.08f;
        ThrowDir.Normalize();
    }

    FVector Velocity = ThrowDir * ThrowImpulseStrength;

    if (const APawn* PawnOwner = Cast<APawn>(GetOwner()))
    {
        const FVector PawnVel = PawnOwner->GetVelocity();
        const float ForwardSpeed = FVector::DotProduct(PawnVel, ThrowDir);
        if (ForwardSpeed > 0.0f)
        {
            Velocity += ThrowDir * (ForwardSpeed * 0.5f);
        }
    }

    return Velocity;
}

bool UPhysicsCarryComponent::CanGrabServer(const AActor* TargetActor, const UPrimitiveComponent* ComponentToGrab) const
{
    if (!IsValid(TargetActor) || !IsValid(ComponentToGrab))
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent][Server] CanGrabServer Denied: Invalid TargetActor or ComponentToGrab."));
        return false;
    }

    if (ComponentToGrab->GetOwner() != TargetActor)
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent][Server] CanGrabServer Denied: Component does not belong to TargetActor."));
        return false;
    }

    if (CarryState != ECarryState::None && CarryState != ECarryState::RequestingGrab)
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent][Server] CanGrabServer Denied: CarryState is not None/RequestingGrab (Current: %d)."),
            static_cast<int32>(CarryState));
        return false;
    }

    const AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return false;
    }

    const IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(TargetActor);
    if (!Grabbable || !Grabbable->CanGrab(Owner))
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent][Server] CanGrabServer Denied: IGrabbable::CanGrab returned false for %s."),
            *GetNameSafe(TargetActor));
        return false;
    }

    if (Grabbable->GetMass() > MaxCarryMass)
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent][Server] CanGrabServer Denied: Mass (%.1f kg) exceeds MaxCarryMass (%.1f kg)."),
            Grabbable->GetMass(), MaxCarryMass);
        return false;
    }

    const float BoundsRadius = TargetActor->GetSimpleCollisionRadius();
    const float AllowedDist = 300.0f + BoundsRadius + 100.0f;
    const float DistSq = FVector::DistSquared(Owner->GetActorLocation(), TargetActor->GetActorLocation());
    if (DistSq > FMath::Square(AllowedDist))
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent][Server] CanGrabServer Denied: Distance (%.1f cm > allowed %.1f cm)."),
            FMath::Sqrt(DistSq), AllowedDist);
        return false;
    }

    return true;
}

void UPhysicsCarryComponent::TryGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor) return;

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastClientInteractionTime < MinInteractionInterval)
    {
        return;
    }

    if (CarryState != ECarryState::None)
    {
        return;
    }

    const IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(TargetActor);
    if (!Grabbable || !Grabbable->CanGrab(GetOwner()))
    {
        return;
    }

    if (Grabbable->GetMass() > MaxCarryMass)
    {
        UE_LOG(LogDungeonInteraction, Warning, TEXT("[PhysicsCarryComponent] Prop mass (%.1f kg) exceeds limit (%.1f kg)."),
            Grabbable->GetMass(), MaxCarryMass);
        return;
    }

    LastClientInteractionTime = CurrentTime;

    if (NetUtils::HasAuthority(this))
    {
        ExecuteGrab(TargetActor, ComponentToGrab);
    }
    else
    {
        CarryState = ECarryState::RequestingGrab;
        Server_RequestGrab(TargetActor, ComponentToGrab);
    }
}

void UPhysicsCarryComponent::DropOrSwing()
{
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastClientInteractionTime < MinInteractionInterval)
    {
        return;
    }
    LastClientInteractionTime = CurrentTime;

    const float SwingSpeed = TrackedCameraSwingVelocity.Size();
    const bool bIsThrow = (SwingSpeed >= MinSwingSpeedToThrow);
    FVector ReleaseVelocity = FVector::ZeroVector;

    if (bIsThrow)
    {
        const float ScaledSpeed = FMath::Clamp(SwingSpeed * SwingVelocityMultiplier, 0.0f, MaxSwingThrowSpeed);
        ReleaseVelocity = TrackedCameraSwingVelocity.GetSafeNormal() * ScaledSpeed;

        UE_LOG(LogDungeonInteraction, Log, TEXT("[PhysicsCarryComponent]%s Mouse Swing Throw! Swing Speed: %.1f cm/s"),
            *NetUtils::GetNetRolePrefix(this), ScaledSpeed);
    }
    else
    {
        UE_LOG(LogDungeonInteraction, Log, TEXT("[PhysicsCarryComponent]%s Pure drop under feet."),
            *NetUtils::GetNetRolePrefix(this));
    }

    if (NetUtils::HasAuthority(this))
    {
        ExecuteRelease(bIsThrow, ReleaseVelocity);
    }
    else
    {
        CarryState = ECarryState::Releasing;
        Server_RequestDropOrSwing(FVector_NetQuantize(ReleaseVelocity));
    }
}

void UPhysicsCarryComponent::ThrowCurrentProp()
{
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastClientInteractionTime < MinInteractionInterval)
    {
        return;
    }
    LastClientInteractionTime = CurrentTime;

    if (NetUtils::HasAuthority(this))
    {
        const FVector LaunchVelocity = CalculateServerThrowVelocity();
        ExecuteRelease(true, LaunchVelocity);
    }
    else
    {
        CarryState = ECarryState::Releasing;
        Server_RequestForwardThrow();
    }
}

void UPhysicsCarryComponent::ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor || !ComponentToGrab) return;

    if (const UWorld* World = GetWorld())
    {
        LastServerInteractionTime = World->GetTimeSeconds();
    }

    GrabbedActor = TargetActor;
    GrabbedComponent = ComponentToGrab;
    CarryState = ECarryState::Carrying;

    const APawn* PawnOwner = Cast<APawn>(GetOwner());
    const bool bIsLocalOwner = PawnOwner && PawnOwner->IsLocallyControlled();
    const bool bHasAuthority = NetUtils::HasAuthority(this);

    if (bHasAuthority || bIsLocalOwner)
    {
        FVector CamLoc;
        FRotator CamRot;
        GetCameraViewPoint(CamLoc, CamRot);
        PreviousCameraRotation = CamRot;
        TrackedCameraSwingVelocity = FVector::ZeroVector;
        SetComponentTickEnabled(true);

        if (GrabbedComponent)
        {
            BindPropOverlapEvents(GrabbedComponent.Get());
        }
    }

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(GrabbedActor))
    {
        Grabbable->OnGrabbed(GetOwner());
    }

    UE_LOG(LogDungeonInteraction, Log, TEXT("[PhysicsCarryComponent]%s Grabbed: %s"), *NetUtils::GetNetRolePrefix(this), *GetNameSafe(GrabbedActor));
}

void UPhysicsCarryComponent::ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity)
{
    if (!GrabbedActor) return;

    if (const UWorld* World = GetWorld())
    {
        LastServerInteractionTime = World->GetTimeSeconds();
    }

    AActor* ReleasedActor = GrabbedActor;
    const FVector AppliedVelocity = bIsThrow ? LaunchVelocity : FVector::ZeroVector;

    ResetGrabState();

    if (IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(ReleasedActor))
    {
        Grabbable->OnDropped(GetOwner(), AppliedVelocity);
    }

    UE_LOG(LogDungeonInteraction, Log, TEXT("[PhysicsCarryComponent]%s Released/Thrown: %s (Velocity: %s)"),
        *NetUtils::GetNetRolePrefix(this), *GetNameSafe(ReleasedActor), *AppliedVelocity.ToString());
}

// -------------------------------------------------------------------------------------------------
// RPC Implementations (Server)
// -------------------------------------------------------------------------------------------------

bool UPhysicsCarryComponent::Server_RequestGrab_Validate(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    return TargetActor != nullptr && ComponentToGrab != nullptr;
}

void UPhysicsCarryComponent::Server_RequestGrab_Implementation(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastServerInteractionTime < MinInteractionInterval)
    {
        Client_GrabDenied();
        return;
    }
    LastServerInteractionTime = CurrentTime;

    if (!CanGrabServer(TargetActor, ComponentToGrab))
    {
        Client_GrabDenied();
        return;
    }

    ExecuteGrab(TargetActor, ComponentToGrab);
}

void UPhysicsCarryComponent::Client_GrabDenied_Implementation()
{
    UE_LOG(LogDungeonNetwork, Warning, TEXT("[PhysicsCarryComponent][Client] Grab request was denied by server."));
    ResetGrabState();
}

bool UPhysicsCarryComponent::Server_RequestForwardThrow_Validate()
{
    return true;
}

void UPhysicsCarryComponent::Server_RequestForwardThrow_Implementation()
{
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastServerInteractionTime < MinInteractionInterval)
    {
        return;
    }
    LastServerInteractionTime = CurrentTime;

    const FVector LaunchVelocity = CalculateServerThrowVelocity();
    ExecuteRelease(true, LaunchVelocity);
}

bool UPhysicsCarryComponent::Server_RequestDropOrSwing_Validate(const FVector_NetQuantize& SwingVelocity)
{
    return !SwingVelocity.ContainsNaN();
}

void UPhysicsCarryComponent::Server_RequestDropOrSwing_Implementation(const FVector_NetQuantize& SwingVelocity)
{
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastServerInteractionTime < MinInteractionInterval)
    {
        return;
    }
    LastServerInteractionTime = CurrentTime;

    const float Speed = SwingVelocity.Size();
    if (Speed < MinSwingSpeedToThrow)
    {
        ExecuteRelease(false, FVector::ZeroVector);
    }
    else
    {
        const float ClampedSpeed = FMath::Min(Speed, MaxSwingThrowSpeed);
        const FVector ValidatedVelocity = SwingVelocity.GetSafeNormal() * ClampedSpeed;
        ExecuteRelease(true, ValidatedVelocity);
    }
}

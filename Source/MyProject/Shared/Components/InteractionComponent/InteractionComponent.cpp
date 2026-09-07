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
    if (InProp)
    {
        GrabbedComponent = Cast<UPrimitiveComponent>(InProp->GetRootComponent());
    }
    CarryState = ECarryState::Carrying;

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
    CarryState = ECarryState::None;
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
        if (Comp->GetPhysicsLinearVelocity().Size() < VelocityStopThreshold)
        {
            Comp->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Comp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
    }
}

FVector UInteractionComponent::CalculateHoldAnchorRelativeOffset(float AimPitch, float BaseEyeHeightOffset) const
{
    const float NormalizedPitch = FRotator::NormalizeAxis(AimPitch);
    const float ClampedPitch = FMath::Clamp(NormalizedPitch, -50.0f, 50.0f);
    const float PitchRad = FMath::DegreesToRadians(ClampedPitch);

    constexpr float HoldDistance = 110.0f;
    const float BaseZ = BaseEyeHeightOffset - 15.0f;
    const float TargetX = HoldDistance * FMath::Cos(PitchRad);
    const float TargetZ = BaseZ + (HoldDistance * FMath::Sin(PitchRad));

    return FVector(TargetX, 0.0f, TargetZ);
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

    // 2. Pozycja relatywna kotwicy wyliczona z czystej matematyki
    const FVector TargetRelLoc = CalculateHoldAnchorRelativeOffset(AimPitch, PlayerChar->BaseEyeHeightOffset);

    // 3. Płynna interpolacja pozycji kotwicy
    const FVector CurrentRelLoc = PlayerChar->HoldAnchorComponent->GetRelativeLocation();
    const FVector NewRelLoc = FMath::VInterpTo(CurrentRelLoc, TargetRelLoc, DeltaTime, 20.0f);

    PlayerChar->HoldAnchorComponent->SetRelativeLocation(NewRelLoc);
}

void UInteractionComponent::UpdateSwingVelocity(float DeltaTime)
{
    FVector CamLoc;
    FRotator CamRot;
    GetCameraViewPoint(CamLoc, CamRot);

    if (!PreviousCameraRotation.IsZero() && DeltaTime > 0.0001f)
    {
        const FRotator DeltaRot = (CamRot - PreviousCameraRotation).GetNormalized();
        const float YawRateRad = FMath::DegreesToRadians(DeltaRot.Yaw / DeltaTime);
        const float PitchRateRad = FMath::DegreesToRadians(DeltaRot.Pitch / DeltaTime);

        // Obliczamy prędkość liniową na ramieniu trzymania (ok. 110 cm)
        constexpr float HoldRadius = 110.0f;
        const FVector TangentialVelocity = (CamRot.RotateVector(FVector::RightVector) * (YawRateRad * HoldRadius))
                                         + (CamRot.RotateVector(FVector::UpVector) * (PitchRateRad * HoldRadius));

        TrackedCameraSwingVelocity = FMath::VInterpTo(TrackedCameraSwingVelocity, TangentialVelocity, DeltaTime, 16.0f);
    }
    PreviousCameraRotation = CamRot;
}

void UInteractionComponent::HandleSweepCollision(const FHitResult& SweepHit, APlayerCharacter* PlayerChar)
{
    if (!SweepHit.bBlockingHit || !PlayerChar) return;

    UPrimitiveComponent* HitComp = SweepHit.GetComponent();
    if (!HitComp || !HitComp->IsSimulatingPhysics()) return;

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

void UInteractionComponent::SuppressOverlappingHeavyPhysics(APlayerCharacter* PlayerChar)
{
    if (!PlayerChar || !IsValid(GrabbedActor)) return;

    UPrimitiveComponent* PropPrim = GrabbedComponent ? GrabbedComponent.Get() : Cast<UPrimitiveComponent>(GrabbedActor->GetRootComponent());
    if (!PropPrim) return;

    TArray<UPrimitiveComponent*> Overlaps;
    PropPrim->GetOverlappingComponents(Overlaps);

    for (UPrimitiveComponent* OverlapComp : Overlaps)
    {
        StopHeavyPhysicsObject(OverlapComp, PlayerChar->MaxPushableMass);
    }
}

bool UInteractionComponent::CheckGripBreakDistance(const FVector& TargetLocation)
{
    if (!IsValid(GrabbedActor)) return false;

    const float DistanceFromHands = FVector::Dist(GrabbedActor->GetActorLocation(), TargetLocation);
    if (DistanceFromHands > CarryBreakDistance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService]%s Carry grip broken! Distance (%.1f cm) exceeded limit (%.1f cm)."),
            *NetUtils::GetNetRolePrefix(this), DistanceFromHands, CarryBreakDistance);

        // Zerwanie chwytu – upuszczenie przedmiotu
        PrimaryInteract();
        return true;
    }

    return false;
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

    // 2. Śledzenie czystej prędkości kątowej kamery (zamach myszką)
    UpdateSwingVelocity(DeltaTime);

    // 3. Reakcja na napotkane przeszkody fizyczne
    HandleSweepCollision(SweepHit, PlayerChar);

    // 4. Zabezpieczenie przed rotacją kamery i szturnięciami od boku
    SuppressOverlappingHeavyPhysics(PlayerChar);

    // 5. Weryfikacja dystansu: czy ręce gracza nie zostały zbyt mocno oddalone od zablokowanego propa
    CheckGripBreakDistance(TargetLocation);
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

FVector UInteractionComponent::CalculateServerThrowVelocity() const
{
    FVector CameraLoc;
    FRotator CameraRot;
    GetCameraViewPoint(CameraLoc, CameraRot);

    FVector ThrowDir = CameraRot.Vector();

    // Lekkie podbicie w górę (Upward bias) przy rzucie horyzontalnym, zapobiega uderzeniu o grunt tuż przed stopami
    if (ThrowDir.Z > -0.2f && ThrowDir.Z < 0.2f)
    {
        ThrowDir.Z += 0.08f;
        ThrowDir.Normalize();
    }

    FVector Velocity = ThrowDir * ThrowImpulseStrength;

    // Dziedziczenie pędu biegu gracza (50% wektora prędkości w kierunku rzutu)
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

bool UInteractionComponent::CanGrabServer(const AActor* TargetActor, const UPrimitiveComponent* ComponentToGrab) const
{
    // 1. Walidacja wskaźników
    if (!IsValid(TargetActor) || !IsValid(ComponentToGrab))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: Invalid TargetActor or ComponentToGrab."));
        return false;
    }

    // 2. Komponent musi należeć do wskazanego aktora
    if (ComponentToGrab->GetOwner() != TargetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: Component does not belong to TargetActor."));
        return false;
    }

    // 3. Stan komponentu gracza: wolne ręce lub aktywne zapytanie o ten obiekt
    if (CarryState != ECarryState::None && CarryState != ECarryState::RequestingGrab)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: CarryState is not None/RequestingGrab (Current: %d)."),
            static_cast<int32>(CarryState));
        return false;
    }

    const AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return false;
    }

    // 4. Kontrakt IGrabbable: czy obiekt jest gotowy do chwytu (żywy, niezniszczony, niechwycony przez kogoś innego)
    const IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(TargetActor);
    if (!Grabbable || !Grabbable->CanGrab(Owner))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: IGrabbable::CanGrab returned false for %s."),
            *GetNameSafe(TargetActor));
        return false;
    }

    // 5. Weryfikacja udźwigu
    if (Grabbable->GetMass() > MaxCarryMass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: Mass (%.1f kg) exceeds MaxCarryMass (%.1f kg)."),
            Grabbable->GetMass(), MaxCarryMass);
        return false;
    }

    // 6. Weryfikacja odległości z marginesem sieciowym i uwzględnieniem promienia bryły propa
    const float BoundsRadius = TargetActor->GetSimpleCollisionRadius();
    const float AllowedDist = TraceDistance + BoundsRadius + 100.0f;
    const float DistSq = FVector::DistSquared(Owner->GetActorLocation(), TargetActor->GetActorLocation());
    if (DistSq > FMath::Square(AllowedDist))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: Distance (%.1f cm > allowed %.1f cm)."),
            FMath::Sqrt(DistSq), AllowedDist);
        return false;
    }

    // 7. Weryfikacja Line of Sight (czy gracz nie chwyta przez ścianę/przeszkodę)
    if (const UWorld* World = GetWorld())
    {
        FVector EyesLoc;
        FRotator EyesRot;
        GetCameraViewPoint(EyesLoc, EyesRot);

        FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(CanGrabServerLoS), false, Owner);
        LoSParams.AddIgnoredActor(Owner);
        LoSParams.AddIgnoredActor(TargetActor);

        // Celujemy w geometryczny środek bryły komponentu zamiast w punkt pivot na posadzce
        const FVector TargetCenter = ComponentToGrab->Bounds.Origin;

        FHitResult LoSHit;
        const bool bHit = World->LineTraceSingleByChannel(
            LoSHit,
            EyesLoc,
            TargetCenter,
            ECC_Visibility,
            LoSParams
        );

        if (bHit && LoSHit.bBlockingHit)
        {
            UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] CanGrabServer Denied: LoS blocked by actor %s (component: %s)."),
                *GetNameSafe(LoSHit.GetActor()), *GetNameSafe(LoSHit.GetComponent()));
            return false;
        }
    }

    return true;
}

void UInteractionComponent::PrimaryInteract()
{
    UE_LOG(LogTemp, Warning, TEXT("[InteractionService]%s PrimaryInteract triggered (State: %d)."), 
        *NetUtils::GetNetRolePrefix(this), static_cast<int32>(CarryState));

    // Jeśli aktywnie niesiemy obiekt: upuszczenie pod nogi LUB rzut zamachem myszką (Klawisz E)
    if (CarryState == ECarryState::Carrying && IsValid(GrabbedActor))
    {
        const float SwingSpeed = TrackedCameraSwingVelocity.Size();
        const bool bIsThrow = (SwingSpeed >= MinSwingSpeedToThrow);
        FVector ReleaseVelocity = FVector::ZeroVector;

        if (bIsThrow)
        {
            const float ScaledSpeed = FMath::Clamp(SwingSpeed * SwingVelocityMultiplier, 0.0f, MaxSwingThrowSpeed);
            ReleaseVelocity = TrackedCameraSwingVelocity.GetSafeNormal() * ScaledSpeed;

            UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Mouse Swing Throw! Swing Speed: %.1f cm/s"),
                *NetUtils::GetNetRolePrefix(this), ScaledSpeed);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Pure drop under feet (Zero launch velocity)."),
                *NetUtils::GetNetRolePrefix(this));
        }

        if (NetUtils::HasAuthority(this))
        {
            ExecuteRelease(bIsThrow, ReleaseVelocity);
        }
        else
        {
            // Klient ustawia stan zwalniania i wysyła intencję upuszczenia lub zamachu
            CarryState = ECarryState::Releasing;
            Server_RequestDropOrSwing(FVector_NetQuantize(ReleaseVelocity));
        }
        return;
    }

    // Jeśli jesteśmy w trakcie przetwarzania poprzedniego żądania (Requesting/Releasing) - ignorujemy spam
    if (CarryState != ECarryState::None)
    {
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
                // Architektura Server-Authoritative: Klient NIE ustawia już GrabbedActor lokalnie przed zgodą serwera!
                CarryState = ECarryState::RequestingGrab;
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
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

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

#if ENABLE_DRAW_DEBUG
    DrawDebugLine(GetWorld(), CameraLocation, OutHit.ImpactPoint, FColor::Green, false, 2.0f, 0, 2.0f);
#endif
    return true;
}

void UInteractionComponent::ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor || !ComponentToGrab) return;

    GrabbedActor = TargetActor;
    GrabbedComponent = ComponentToGrab;
    CarryState = ECarryState::Carrying;

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
    // RPC Param Validation: czy pakiet nie zawiera uszkodzonych/pustych wskaźników
    return TargetActor != nullptr && ComponentToGrab != nullptr;
}

void UInteractionComponent::Server_RequestGrab_Implementation(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    // Gameplay Validation: Dystans, LoS, Masa, Stan obiektu i zajętość
    if (!CanGrabServer(TargetActor, ComponentToGrab))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Server] Denied Grab: Target %s failed gameplay validation."), 
            *GetNameSafe(TargetActor));
        Client_GrabDenied();
        return;
    }

    ExecuteGrab(TargetActor, ComponentToGrab);
}

void UInteractionComponent::Client_GrabDenied_Implementation()
{
    UE_LOG(LogTemp, Warning, TEXT("[InteractionService][Client] Grab request was denied by server."));
    ResetGrabState();
}

bool UInteractionComponent::Server_RequestForwardThrow_Validate()
{
    return true;
}

void UInteractionComponent::Server_RequestForwardThrow_Implementation()
{
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

    const FVector LaunchVelocity = CalculateServerThrowVelocity();
    ExecuteRelease(true, LaunchVelocity);
}

bool UInteractionComponent::Server_RequestDropOrSwing_Validate(const FVector_NetQuantize& SwingVelocity)
{
    return !SwingVelocity.ContainsNaN();
}

void UInteractionComponent::Server_RequestDropOrSwing_Implementation(const FVector_NetQuantize& SwingVelocity)
{
    if (CarryState != ECarryState::Carrying || !IsValid(GrabbedActor))
    {
        return;
    }

    const float Speed = SwingVelocity.Size();
    if (Speed < MinSwingSpeedToThrow)
    {
        // Czyste upuszczenie pod stopy
        ExecuteRelease(false, FVector::ZeroVector);
    }
    else
    {
        // Rzut zamachem myszką (clamping prędkości dla ochrony sieci i fizyki)
        const float ClampedSpeed = FMath::Min(Speed, MaxSwingThrowSpeed);
        const FVector ValidatedVelocity = SwingVelocity.GetSafeNormal() * ClampedSpeed;
        ExecuteRelease(true, ValidatedVelocity);
    }
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

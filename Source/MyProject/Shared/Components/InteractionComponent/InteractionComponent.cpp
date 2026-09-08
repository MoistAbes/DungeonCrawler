#include "InteractionComponent.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Interfaces/CarryAnchorProviderInterface.h"
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

    const APawn* PawnOwner = Cast<APawn>(GetOwner());
    const bool bIsLocalOwner = PawnOwner && PawnOwner->IsLocallyControlled();
    const bool bHasAuthority = NetUtils::HasAuthority(this);

    // Zdalni gracze (Remote Proxies) nie wykonują lokalnego sweepa ani fizyki.
    // Otrzymują oni zreplikowany i wygładzony ruch propa bezpośrednio z silnika (ReplicatedMovement).
    if (!bHasAuthority && !bIsLocalOwner)
    {
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

    const APawn* PawnOwner = Cast<APawn>(GetOwner());
    const bool bIsLocalOwner = PawnOwner && PawnOwner->IsLocallyControlled();
    const bool bHasAuthority = NetUtils::HasAuthority(this);

    // Włączamy On-Demand Tick wyłącznie na Serwerze oraz u lokalnie kontrolującego gracza
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
    UnbindPropOverlapEvents();

    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
    CarryState = ECarryState::None;
    PreviousCameraRotation = FRotator::ZeroRotator;
    TrackedCameraSwingVelocity = FVector::ZeroVector;
    SetComponentTickEnabled(false);
}

void UInteractionComponent::BindPropOverlapEvents(UPrimitiveComponent* PropComp)
{
    UnbindPropOverlapEvents();

    if (!IsValid(PropComp)) return;

    PropComp->SetGenerateOverlapEvents(true);
    PropComp->OnComponentBeginOverlap.AddDynamic(this, &UInteractionComponent::OnPropBeginOverlap);
    PropComp->OnComponentEndOverlap.AddDynamic(this, &UInteractionComponent::OnPropEndOverlap);

    // Wstępne pobranie overlapów na moment chwytu (jednorazowo)
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

void UInteractionComponent::UnbindPropOverlapEvents()
{
    if (GrabbedComponent)
    {
        GrabbedComponent->OnComponentBeginOverlap.RemoveDynamic(this, &UInteractionComponent::OnPropBeginOverlap);
        GrabbedComponent->OnComponentEndOverlap.RemoveDynamic(this, &UInteractionComponent::OnPropEndOverlap);
    }
    OverlappingPhysicsComponents.Reset();
}

void UInteractionComponent::OnPropBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!IsValid(OtherComp) || OtherActor == GetOwner() || OtherActor == GrabbedActor)
    {
        return;
    }

    if (OtherComp->IsSimulatingPhysics())
    {
        OverlappingPhysicsComponents.AddUnique(OtherComp);
    }
}

void UInteractionComponent::OnPropEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!IsValid(OtherComp))
    {
        return;
    }

    OverlappingPhysicsComponents.Remove(OtherComp);
}

void UInteractionComponent::StopHeavyPhysicsObject(UPrimitiveComponent* Comp, float MaxPushableMass)
{
    UKineticForceLibrary::SuppressHeavyPhysicsJitter(Comp, MaxPushableMass, VelocityStopThreshold);
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

    ICarryAnchorProviderInterface* CarryProvider = Cast<ICarryAnchorProviderInterface>(PawnOwner);
    if (!CarryProvider) return;

    USceneComponent* HoldAnchor = CarryProvider->GetHoldAnchorComponent();
    if (!HoldAnchor) return;

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
    const FVector TargetRelLoc = CalculateHoldAnchorRelativeOffset(AimPitch, CarryProvider->GetCarryEyeHeightOffset());

    // 3. Płynna interpolacja pozycji kotwicy
    const FVector CurrentRelLoc = HoldAnchor->GetRelativeLocation();
    const FVector NewRelLoc = FMath::VInterpTo(CurrentRelLoc, TargetRelLoc, DeltaTime, 20.0f);

    HoldAnchor->SetRelativeLocation(NewRelLoc);
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

void UInteractionComponent::HandleSweepCollision(const FHitResult& SweepHit, ICarryAnchorProviderInterface* CarryProvider, AActor* CarrierActor)
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

void UInteractionComponent::SuppressOverlappingHeavyPhysics(ICarryAnchorProviderInterface* CarryProvider)
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

    AActor* OwnerActor = GetOwner();
    ICarryAnchorProviderInterface* CarryProvider = Cast<ICarryAnchorProviderInterface>(OwnerActor);
    if (!CarryProvider) return;

    USceneComponent* HoldAnchor = CarryProvider->GetHoldAnchorComponent();
    if (!HoldAnchor) return;

    const FVector TargetLocation = HoldAnchor->GetComponentLocation();
    const FRotator TargetRotation = HoldAnchor->GetComponentRotation();

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
    HandleSweepCollision(SweepHit, CarryProvider, OwnerActor);

    // 4. Zabezpieczenie przed rotacją kamery i szturnięciami od boku (zdarzeniowe bez broadphase co klatkę)
    SuppressOverlappingHeavyPhysics(CarryProvider);

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
            InteractionChannel,
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
    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastClientInteractionTime < MinInteractionInterval)
    {
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[InteractionService]%s PrimaryInteract triggered (State: %d)."), 
        *NetUtils::GetNetRolePrefix(this), static_cast<int32>(CarryState));

    // Jeśli aktywnie niesiemy obiekt: upuszczenie pod nogi LUB rzut zamachem myszką (Klawisz E)
    if (CarryState == ECarryState::Carrying && IsValid(GrabbedActor))
    {
        LastClientInteractionTime = CurrentTime;

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

            LastClientInteractionTime = CurrentTime;

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
            LastClientInteractionTime = CurrentTime;

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

bool UInteractionComponent::PerformTrace(FHitResult& OutHit) const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return false;

    const UWorld* World = GetWorld();
    if (!World) return false;

    FVector CameraLocation;
    FRotator CameraRotation;
    GetCameraViewPoint(CameraLocation, CameraRotation);

    const float ExtendedTraceDistance = TraceDistance + 1000.0f;
    const FVector TraceEnd = CameraLocation + (CameraRotation.Vector() * ExtendedTraceDistance);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionTrace), false, Owner);
    Params.AddIgnoredActor(Owner);

    bool bHit = false;
    if (InteractionTraceRadius > 0.0f)
    {
        const FCollisionShape SphereShape = FCollisionShape::MakeSphere(InteractionTraceRadius);
        bHit = World->SweepSingleByChannel(
            OutHit,
            CameraLocation,
            TraceEnd,
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
            CameraLocation,
            TraceEnd,
            InteractionChannel,
            Params
        );
    }

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
    if (InteractionTraceRadius > 0.0f)
    {
        DrawDebugSphere(World, OutHit.ImpactPoint, InteractionTraceRadius, 12, FColor::Green, false, 2.0f, 0, 1.5f);
    }
    else
    {
        DrawDebugLine(World, CameraLocation, OutHit.ImpactPoint, FColor::Green, false, 2.0f, 0, 2.0f);
    }
#endif
    return true;
}

void UInteractionComponent::ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
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

    UE_LOG(LogTemp, Log, TEXT("[InteractionService]%s Grabbed: %s"), *NetUtils::GetNetRolePrefix(this), *GetNameSafe(GrabbedActor));
}

void UInteractionComponent::ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity)
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
    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastServerInteractionTime < MinInteractionInterval)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[InteractionService][Server] Denied Grab: Rate limit exceeded (Delta: %.3f s < %.3f s)."),
            CurrentTime - LastServerInteractionTime, MinInteractionInterval);
        Client_GrabDenied();
        return;
    }
    LastServerInteractionTime = CurrentTime;

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

    const UWorld* World = GetWorld();
    if (!World) return;

    const double CurrentTime = World->GetTimeSeconds();
    if (CurrentTime - LastServerInteractionTime < MinInteractionInterval)
    {
        return;
    }
    LastServerInteractionTime = CurrentTime;

    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
    {
        if (Interactable->CanInteract(GetOwner()))
        {
            Interactable->Interact(GetOwner());
        }
    }
}

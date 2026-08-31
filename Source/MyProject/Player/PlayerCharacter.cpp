#include "PlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/UserWidget.h"
#include "MyProject/MyProject.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Interaction/Components/InteractionComponent/InteractionComponent.h"
#include "MyProject/UI/PlayerHUDWidget/PlayerHUDWidget.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

APlayerCharacter::APlayerCharacter()
{
    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionCylinder"));
    RootComponent = CapsuleComponent;
    CapsuleComponent->InitCapsuleSize(35.0f, 90.0f);
    
    CapsuleComponent->SetCollisionObjectType(ECC_Pawn);
    CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Block);
    CapsuleComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    CapsuleComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    CapsuleComponent->SetSimulatePhysics(false);
    CapsuleComponent->SetNotifyRigidBodyCollision(false);

    MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(CapsuleComponent);
    MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
    MeshComponent->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
    MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
    MeshComponent->SetGenerateOverlapEvents(false);

    SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArmComponent->SetupAttachment(CapsuleComponent);
    SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeightOffset));
    SpringArmComponent->bUsePawnControlRotation = true;
    TargetArmLength = 400.0f;
    SpringArmComponent->TargetArmLength = TargetArmLength;

    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
    CameraComponent->bUsePawnControlRotation = false;

    PhysicsHandleComponent = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandleComponent"));
    PhysicsHandleComponent->LinearDamping = 200.0f;
    PhysicsHandleComponent->LinearStiffness = 1500.0f;
    PhysicsHandleComponent->AngularDamping = 200.0f;
    PhysicsHandleComponent->AngularStiffness = 1500.0f;
    PhysicsHandleComponent->InterpolationSpeed = 50.0f;
    PhysicsHandleComponent->bInterpolateTarget = true;

    InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
    DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));

    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    bIsZooming = false;
}

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (CapsuleComponent)
    {
        CapsuleComponent->SetCapsuleSize(35.0f, 90.0f);
        CapsuleComponent->SetCollisionObjectType(ECC_Pawn);
        CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Block);
        CapsuleComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        CapsuleComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        CapsuleComponent->SetSimulatePhysics(false);
    }

    if (SpringArmComponent)
    {
        SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeightOffset));
    }

    if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }

        if (HUDWidgetClass && PlayerController->IsLocalController())
        {
            ActiveHUDWidget = CreateWidget<UPlayerHUDWidget>(PlayerController, HUDWidgetClass);
            if (ActiveHUDWidget)
            {
                ActiveHUDWidget->AddToViewport();
                ActiveHUDWidget->BindHealthComponent(DamageableComponent);
            }
        }
    }

    FHitResult InitialGroundHit;
    PerformGroundCheck(InitialGroundHit);
    UpdateTickState();
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (MoveAction)
        {
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &APlayerCharacter::MoveCompleted);
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &APlayerCharacter::MoveCompleted);
        }

        if (LookAction)
        {
            EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
        }

        if (ZoomAction)
        {
            EnhancedInputComponent->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Zoom);
        }

        if (InteractAction)
        {
            EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleInteract);
        }

        if (ThrowAction)
        {
            EnhancedInputComponent->BindAction(ThrowAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleThrow);
        }

        if (JumpAction)
        {
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &APlayerCharacter::Jump);
        }
    }
}

void APlayerCharacter::UpdateTickState()
{
    const bool bNeedsMovementTick = bMovementInputActive 
        || !bIsGrounded 
        || !CurrentVelocity.IsNearlyZero(1.0f) 
        || bIsKnockedBack 
        || CurrentBaseComponent.IsValid();

    const bool bNeedsTick = bNeedsMovementTick || bIsZooming;

    if (IsActorTickEnabled() != bNeedsTick)
    {
        SetActorTickEnabled(bNeedsTick);
    }
}

void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bIsZooming && SpringArmComponent)
    {
        const float CurrentLength = SpringArmComponent->TargetArmLength;
        if (!FMath::IsNearlyEqual(CurrentLength, TargetArmLength, 0.1f))
        {
            SpringArmComponent->TargetArmLength = FMath::FInterpTo(CurrentLength, TargetArmLength, DeltaTime, 15.0f);
        }
        else
        {
            SpringArmComponent->TargetArmLength = TargetArmLength;
            bIsZooming = false;
        }
    }

    UpdateBaseTracking(DeltaTime);
    PerformMovement(DeltaTime);
    UpdateTickState();
}

bool APlayerCharacter::PerformGroundCheck(FHitResult& OutHitResult)
{
    if (!CapsuleComponent || !GetWorld())
    {
        bIsGrounded = false;
        return false;
    }

    const float Radius = CapsuleComponent->GetScaledCapsuleRadius();
    const float HalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
    const FVector Start = CapsuleComponent->GetComponentLocation();
    const FVector End = Start - FVector(0.0f, 0.0f, GroundCheckDistance + 5.0f);

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);

    // Kapsułowy sweep o pełnych wymiarach kapsuły w dół
    const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    const bool bHit = GetWorld()->SweepSingleByChannel(
        OutHitResult,
        Start,
        End,
        FQuat::Identity,
        ECC_Visibility,
        CapsuleShape,
        QueryParams
    );

    bIsGrounded = bHit && OutHitResult.bBlockingHit && (OutHitResult.ImpactNormal.Z >= WalkableFloorZ);
    return bIsGrounded;
}

void APlayerCharacter::UpdateBaseTracking(float DeltaTime)
{
    FHitResult GroundHit;
    const bool bGrounded = PerformGroundCheck(GroundHit);

    if (bGrounded && GroundHit.GetComponent())
    {
        UPrimitiveComponent* BaseComp = GroundHit.GetComponent();
        if (BaseComp && BaseComp->Mobility == EComponentMobility::Movable)
        {
            if (CurrentBaseComponent == BaseComp)
            {
                const FVector DeltaLocation = BaseComp->GetComponentLocation() - PreviousBaseLocation;
                const FRotator DeltaRotation = BaseComp->GetComponentRotation() - PreviousBaseRotation;

                if (!DeltaLocation.IsNearlyZero())
                {
                    FHitResult PlatformMoveHit;
                    AddActorWorldOffset(DeltaLocation, true, &PlatformMoveHit);
                }

                if (!FMath::IsNearlyZero(DeltaRotation.Yaw))
                {
                    AddActorWorldRotation(FRotator(0.0f, DeltaRotation.Yaw, 0.0f));
                }
            }

            CurrentBaseComponent = BaseComp;
            PreviousBaseLocation = BaseComp->GetComponentLocation();
            PreviousBaseRotation = BaseComp->GetComponentRotation();
            return;
        }
    }

    CurrentBaseComponent = nullptr;
}

void APlayerCharacter::HandlePropSweepHit(const FHitResult& Hit, float DeltaTime)
{
    UPrimitiveComponent* HitComp = Hit.GetComponent();
    if (!HitComp || !HitComp->IsSimulatingPhysics())
    {
        return;
    }

    // Nie popychamy obiektu wyłącznie wtedy, gdy stabilnie stoimy na nim obiema stopami
    if (CurrentBaseComponent.IsValid() && CurrentBaseComponent.Get() == HitComp && bIsGrounded)
    {
        return;
    }

    const float PropMass = HitComp->GetMass();
    if (PropMass <= PushablePropMassThreshold)
    {
        FVector PushDir = -Hit.ImpactNormal;
        PushDir.Z = 0.0f;
        PushDir = PushDir.GetSafeNormal();

        if (PushDir.IsNearlyZero())
        {
            PushDir = DesiredMoveDirection.IsNearlyZero() ? CapsuleComponent->GetForwardVector() : DesiredMoveDirection;
        }

        const float MassRatio = FMath::Clamp(PropMass / PushablePropMassThreshold, 0.1f, 1.0f);
        const float PushMultiplier = FMath::Lerp(2.2f, 0.8f, MassRatio);
        const float PushImpulse = BasePushImpulse * PushMultiplier * (DeltaTime * 60.0f);

        HitComp->WakeRigidBody();
        HitComp->AddImpulse(PushDir * PushImpulse);
    }
}

void APlayerCharacter::PerformMovement(float DeltaTime)
{
    // 1. Sprawdzenie stanu podłoża przed ruchem
    FHitResult GroundHit;
    PerformGroundCheck(GroundHit);

    // 2. Sterowanie prędkością (WASD na ziemi, balistyczna parabola w powietrzu)
    if (bIsGrounded)
    {
        const FVector TargetVelocity2D = DesiredMoveDirection * MaxWalkSpeed;
        const float InterpSpeed = bMovementInputActive ? AccelerationResponsiveness : DecelerationResponsiveness;

        CurrentVelocity.X = FMath::FInterpTo(CurrentVelocity.X, TargetVelocity2D.X, DeltaTime, InterpSpeed);
        CurrentVelocity.Y = FMath::FInterpTo(CurrentVelocity.Y, TargetVelocity2D.Y, DeltaTime, InterpSpeed);

        if (!bMovementInputActive && CurrentVelocity.SizeSquared2D() < 4.0f)
        {
            CurrentVelocity.X = 0.0f;
            CurrentVelocity.Y = 0.0f;
        }

        if (CurrentVelocity.Z <= 0.0f)
        {
            CurrentVelocity.Z = 0.0f;
        }
    }
    else
    {
        const float GravityZ = GetWorld()->GetDefaultGravityZ() * GravityScale;
        CurrentVelocity.Z += GravityZ * DeltaTime;
        CurrentVelocity.Z = FMath::Clamp(CurrentVelocity.Z, -4000.0f, 2000.0f);
    }

    // 3. Wytłumianie odrzutu (Knockback)
    if (bIsKnockedBack)
    {
        KnockbackVelocity = FMath::VInterpTo(KnockbackVelocity, FVector::ZeroVector, DeltaTime, KnockbackDamping);
        if (KnockbackVelocity.SizeSquared() < 25.0f)
        {
            KnockbackVelocity = FVector::ZeroVector;
            bIsKnockedBack = false;
        }
    }

    const FVector TotalVelocity = CurrentVelocity + KnockbackVelocity;
    FVector RemainingDelta = TotalVelocity * DeltaTime;

    if (RemainingDelta.IsNearlyZero())
    {
        return;
    }

    // 4. Czysty, standardowy ślizg kinematyczny (Flat Iterative Slide)
    for (int32 Iteration = 0; Iteration < 3 && !RemainingDelta.IsNearlyZero(); ++Iteration)
    {
        FHitResult Hit;
        AddActorWorldOffset(RemainingDelta, true, &Hit);

        if (!Hit.bBlockingHit)
        {
            break;
        }

        HandlePropSweepHit(Hit, DeltaTime);

        // Obrażenia kinetyczne przy knockbacku
        if (bIsKnockedBack && Hit.ImpactNormal.Z < WalkableFloorZ)
        {
            const float ImpactSpeed = FMath::Abs(FVector::DotProduct(TotalVelocity, Hit.ImpactNormal));
            if (ImpactSpeed > KnockbackDamageImpactThreshold && DamageableComponent)
            {
                DamageableComponent->ApplyKineticImpact(ImpactSpeed);
            }
        }

        // Uderzenie w sufit
        if (Hit.ImpactNormal.Z <= -0.5f && CurrentVelocity.Z > 0.0f)
        {
            CurrentVelocity.Z = 0.0f;
        }

        // Ślizg po powierzchni geometrycznej
        const float AppliedFraction = Hit.Time;
        RemainingDelta = RemainingDelta * (1.0f - AppliedFraction);
        RemainingDelta = FVector::VectorPlaneProject(RemainingDelta, Hit.ImpactNormal);

        if (bIsGrounded || CurrentVelocity.Z <= 0.0f)
        {
            const float VelocityIntoSurface = FVector::DotProduct(CurrentVelocity, Hit.ImpactNormal);
            if (VelocityIntoSurface < 0.0f)
            {
                CurrentVelocity -= Hit.ImpactNormal * VelocityIntoSurface;
            }
        }
    }

    // 5. Finalna weryfikacja podłoża
    FHitResult FinalGroundHit;
    PerformGroundCheck(FinalGroundHit);
}

void APlayerCharacter::ApplyKnockback(const FVector& Impulse)
{
    KnockbackVelocity += Impulse;
    bIsKnockedBack = true;
    bIsGrounded = false;
    UpdateTickState();
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D MovementVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawRotation(0, Rotation.Yaw, 0);

        const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        DesiredMoveDirection = (ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X).GetSafeNormal();
        bMovementInputActive = !DesiredMoveDirection.IsNearlyZero();
        UpdateTickState();
    }
}

void APlayerCharacter::MoveCompleted()
{
    DesiredMoveDirection = FVector::ZeroVector;
    bMovementInputActive = false;
    UpdateTickState();
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D LookAxisVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        AddControllerYawInput(LookAxisVector.X);
        AddControllerPitchInput(LookAxisVector.Y);
    }
}

void APlayerCharacter::Zoom(const FInputActionValue& Value)
{
    const float ZoomValue = Value.Get<float>() * -1.0f;
    TargetArmLength = FMath::Clamp(TargetArmLength + (ZoomValue * ZoomStep), 0.0f, MaxZoomLength);

    if (!bIsZooming)
    {
        bIsZooming = true;
        UpdateTickState();
    }
}

void APlayerCharacter::Jump()
{
    FHitResult GroundHit;
    if (bIsGrounded || PerformGroundCheck(GroundHit))
    {
        CurrentVelocity.Z = JumpImpulseVelocity;
        bIsGrounded = false;
        UpdateTickState();
    }
}

void APlayerCharacter::HandleInteract()
{
    if (InteractionComponent)
    {
        InteractionComponent->PrimaryInteract();
    }
}

void APlayerCharacter::HandleThrow()
{
    if (InteractionComponent)
    {
        InteractionComponent->ThrowCurrentProp();
    }
}

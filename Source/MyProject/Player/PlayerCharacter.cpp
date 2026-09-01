#include "PlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Interaction/Components/InteractionComponent/InteractionComponent.h"
#include "MyProject/UI/PlayerHUDWidget/PlayerHUDWidget.h"


APlayerCharacter::APlayerCharacter()
{
    /*
     * ACharacter already provides:
     *
     * - CapsuleComponent
     * - Mesh
     * - CharacterMovementComponent
     *
     * We intentionally do NOT recreate those components.
     */

    // -------------------------------------------------------------------------
    // Collision
    // -------------------------------------------------------------------------

    GetCapsuleComponent()->InitCapsuleSize(35.0f, 90.0f);

    GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    GetCapsuleComponent()->SetCollisionEnabled(
        ECollisionEnabled::QueryAndPhysics);

    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);

    GetCapsuleComponent()->SetCollisionResponseToChannel(
        ECC_Camera,
        ECR_Ignore);

    GetCapsuleComponent()->SetCollisionResponseToChannel(
        ECC_Visibility,
        ECR_Ignore);

    GetCapsuleComponent()->SetSimulatePhysics(false);
    GetCapsuleComponent()->SetNotifyRigidBodyCollision(false);
    GetCapsuleComponent()->SetHiddenInGame(false);


    // -------------------------------------------------------------------------
    // Mesh
    // -------------------------------------------------------------------------

    GetMesh()->SetRelativeLocation(
        FVector(0.0f, 0.0f, -90.0f));

    GetMesh()->SetRelativeRotation(
        FRotator(0.0f, -90.0f, 0.0f));

    GetMesh()->SetCollisionProfileName(TEXT("NoCollision"));
    GetMesh()->SetGenerateOverlapEvents(false);


    // -------------------------------------------------------------------------
    // Character Movement Component
    //
    // We intentionally use the standard CMC.
    //
    // These values replace the corresponding values from the old custom
    // movement implementation.
    // -------------------------------------------------------------------------

    UCharacterMovementComponent* MovementComponent =
        GetCharacterMovement();

    if (MovementComponent)
    {
        // Old MaxWalkSpeed = 600
        MovementComponent->MaxWalkSpeed = 600.0f;

        // Old GravityScale = 1.8
        MovementComponent->GravityScale = 1.8f;

        // Old JumpImpulseVelocity = 600
        MovementComponent->JumpZVelocity = 600.0f;

        // Old WalkableFloorZ = 0.7
        //
        // Keep Unreal's floor detection and movement solver.
        MovementComponent->SetWalkableFloorZ(0.7f);

        /*
         * IMPORTANT:
         *
         * We deliberately do NOT try to translate:
         *
         * AccelerationResponsiveness = 18
         * DecelerationResponsiveness = 25
         *
         * directly into arbitrary CMC values yet.
         *
         * First we want to see the standard CMC behaviour.
         * Then we tune MaxAcceleration / BrakingDecelerationWalking /
         * GroundFriction based on the actual feel we want.
         */
    }


    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------

    SpringArmComponent =
        CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));

    SpringArmComponent->SetupAttachment(
        GetCapsuleComponent());

    SpringArmComponent->SetRelativeLocation(
        FVector(0.0f, 0.0f, BaseEyeHeightOffset));

    SpringArmComponent->bUsePawnControlRotation = true;

    TargetArmLength = 400.0f;

    SpringArmComponent->TargetArmLength =
        TargetArmLength;


    CameraComponent =
        CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));

    CameraComponent->SetupAttachment(
        SpringArmComponent,
        USpringArmComponent::SocketName);

    CameraComponent->bUsePawnControlRotation = false;


    // -------------------------------------------------------------------------
    // Physics Handle
    // -------------------------------------------------------------------------

    PhysicsHandleComponent =
        CreateDefaultSubobject<UPhysicsHandleComponent>(
            TEXT("PhysicsHandleComponent"));

    PhysicsHandleComponent->LinearDamping = 200.0f;
    PhysicsHandleComponent->LinearStiffness = 1500.0f;

    PhysicsHandleComponent->AngularDamping = 200.0f;
    PhysicsHandleComponent->AngularStiffness = 1500.0f;

    PhysicsHandleComponent->InterpolationSpeed = 50.0f;
    PhysicsHandleComponent->bInterpolateTarget = true;


    // -------------------------------------------------------------------------
    // Gameplay Components
    // -------------------------------------------------------------------------

    InteractionComponent =
        CreateDefaultSubobject<UInteractionComponent>(
            TEXT("InteractionComponent"));

    DamageableComponent =
        CreateDefaultSubobject<UDamageableComponent>(
            TEXT("DamageableComponent"));


    // -------------------------------------------------------------------------
    // Tick
    //
    // CharacterMovementComponent has its own movement tick.
    //
    // The Actor tick is ONLY used here for smooth camera zoom.
    // -------------------------------------------------------------------------

    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    bIsZooming = false;
}


void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();


    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------

    if (SpringArmComponent)
    {
        SpringArmComponent->SetRelativeLocation(
            FVector(0.0f, 0.0f, BaseEyeHeightOffset));
    }


    // -------------------------------------------------------------------------
    // Enhanced Input
    // -------------------------------------------------------------------------

    if (APlayerController* PlayerController =
        Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<
                UEnhancedInputLocalPlayerSubsystem>(
                    PlayerController->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(
                    DefaultMappingContext,
                    0);
            }
        }


        // ---------------------------------------------------------------------
        // HUD
        // ---------------------------------------------------------------------

        if (HUDWidgetClass &&
            PlayerController->IsLocalController())
        {
            ActiveHUDWidget =
                CreateWidget<UPlayerHUDWidget>(
                    PlayerController,
                    HUDWidgetClass);

            if (ActiveHUDWidget)
            {
                ActiveHUDWidget->AddToViewport();

                ActiveHUDWidget->BindHealthComponent(
                    DamageableComponent);
            }
        }
    }
}


void APlayerCharacter::SetupPlayerInputComponent(
    UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(
        PlayerInputComponent);

    UEnhancedInputComponent* EnhancedInputComponent =
        Cast<UEnhancedInputComponent>(
            PlayerInputComponent);

    if (!EnhancedInputComponent)
    {
        return;
    }


    // -------------------------------------------------------------------------
    // Movement
    // -------------------------------------------------------------------------

    if (MoveAction)
    {
        EnhancedInputComponent->BindAction(
            MoveAction,
            ETriggerEvent::Triggered,
            this,
            &APlayerCharacter::Move);
    }


    // -------------------------------------------------------------------------
    // Look
    // -------------------------------------------------------------------------

    if (LookAction)
    {
        EnhancedInputComponent->BindAction(
            LookAction,
            ETriggerEvent::Triggered,
            this,
            &APlayerCharacter::Look);
    }


    // -------------------------------------------------------------------------
    // Zoom
    // -------------------------------------------------------------------------

    if (ZoomAction)
    {
        EnhancedInputComponent->BindAction(
            ZoomAction,
            ETriggerEvent::Triggered,
            this,
            &APlayerCharacter::Zoom);
    }


    // -------------------------------------------------------------------------
    // Interaction
    // -------------------------------------------------------------------------

    if (InteractAction)
    {
        EnhancedInputComponent->BindAction(
            InteractAction,
            ETriggerEvent::Started,
            this,
            &APlayerCharacter::HandleInteract);
    }


    // -------------------------------------------------------------------------
    // Throw
    // -------------------------------------------------------------------------

    if (ThrowAction)
    {
        EnhancedInputComponent->BindAction(
            ThrowAction,
            ETriggerEvent::Started,
            this,
            &APlayerCharacter::HandleThrow);
    }


    // -------------------------------------------------------------------------
    // Jump
    //
    // This is now handled by ACharacter / CMC.
    //
    // Epic's recommended Enhanced Input setup is to call:
    //
    // Started   -> Jump()
    // Completed -> StopJumping()
    //
    // rather than manually changing Velocity.Z.
    // -------------------------------------------------------------------------

    if (JumpAction)
    {
        EnhancedInputComponent->BindAction(
            JumpAction,
            ETriggerEvent::Started,
            this,
            &ACharacter::Jump);

        EnhancedInputComponent->BindAction(
            JumpAction,
            ETriggerEvent::Completed,
            this,
            &ACharacter::StopJumping);

        EnhancedInputComponent->BindAction(
            JumpAction,
            ETriggerEvent::Canceled,
            this,
            &ACharacter::StopJumping);
    }
}


void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bIsZooming || !SpringArmComponent)
    {
        return;
    }

    const float CurrentLength =
        SpringArmComponent->TargetArmLength;

    if (!FMath::IsNearlyEqual(
            CurrentLength,
            TargetArmLength,
            0.1f))
    {
        SpringArmComponent->TargetArmLength =
            FMath::FInterpTo(
                CurrentLength,
                TargetArmLength,
                DeltaTime,
                15.0f);
    }
    else
    {
        SpringArmComponent->TargetArmLength =
            TargetArmLength;

        bIsZooming = false;

        UpdateZoomTickState();
    }
}


void APlayerCharacter::Move(
    const FInputActionValue& Value)
{
    const FVector2D MovementVector =
        Value.Get<FVector2D>();

    if (!Controller)
    {
        return;
    }


    // -------------------------------------------------------------------------
    // Camera-relative movement
    // -------------------------------------------------------------------------

    const FRotator ControlRotation =
        Controller->GetControlRotation();

    const FRotator YawRotation(
        0.0f,
        ControlRotation.Yaw,
        0.0f);


    const FVector ForwardDirection =
        FRotationMatrix(YawRotation)
            .GetUnitAxis(EAxis::X);

    const FVector RightDirection =
        FRotationMatrix(YawRotation)
            .GetUnitAxis(EAxis::Y);


    /*
     * IMPORTANT:
     *
     * We no longer calculate DesiredMoveDirection.
     *
     * We simply feed movement input into the CharacterMovementComponent.
     *
     * CMC handles:
     *
     * - acceleration
     * - velocity
     * - braking
     * - collision
     * - floor detection
     * - walking
     * - falling
     * - networking
     */
    
    AddMovementInput(
        ForwardDirection,
        MovementVector.Y);

    AddMovementInput(
        RightDirection,
        MovementVector.X);
}


void APlayerCharacter::Look(
    const FInputActionValue& Value)
{
    const FVector2D LookAxisVector =
        Value.Get<FVector2D>();

    if (!Controller)
    {
        return;
    }

    AddControllerYawInput(
        LookAxisVector.X);

    AddControllerPitchInput(
        LookAxisVector.Y);
}


void APlayerCharacter::Zoom(
    const FInputActionValue& Value)
{
    const float ZoomValue =
        Value.Get<float>() * -1.0f;

    TargetArmLength =
        FMath::Clamp(
            TargetArmLength +
            (ZoomValue * ZoomStep),
            0.0f,
            MaxZoomLength);


    if (!bIsZooming)
    {
        bIsZooming = true;

        UpdateZoomTickState();
    }
}


void APlayerCharacter::UpdateZoomTickState()
{
    const bool bShouldTick = bIsZooming;

    if (IsActorTickEnabled() != bShouldTick)
    {
        SetActorTickEnabled(
            bShouldTick);
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

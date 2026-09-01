#include "PlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Combat/Components/KnockbackComponent/KnockbackComponent.h"
#include "MyProject/Interaction/Components/InteractionComponent/InteractionComponent.h"
#include "MyProject/Player/Components/PlayerCameraComponent/PlayerCameraComponent.h"


APlayerCharacter::APlayerCharacter()
{
    /*
     * ACharacter already provides:
     * - CapsuleComponent
     * - Mesh
     * - CharacterMovementComponent
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
    // -------------------------------------------------------------------------

    UCharacterMovementComponent* MovementComponent =
        GetCharacterMovement();

    if (MovementComponent)
    {
        MovementComponent->MaxWalkSpeed = 600.0f;
        MovementComponent->GravityScale = 1.8f;
        MovementComponent->JumpZVelocity = 600.0f;
        MovementComponent->SetWalkableFloorZ(0.7f);
    }


    // -------------------------------------------------------------------------
    // Camera Components
    // -------------------------------------------------------------------------

    SpringArmComponent =
        CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));

    SpringArmComponent->SetupAttachment(
        GetCapsuleComponent());

    SpringArmComponent->SetRelativeLocation(
        FVector(0.0f, 0.0f, BaseEyeHeightOffset));

    SpringArmComponent->bUsePawnControlRotation = true;
    SpringArmComponent->TargetArmLength = 400.0f;


    CameraComponent =
        CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));

    CameraComponent->SetupAttachment(
        SpringArmComponent,
        USpringArmComponent::SocketName);

    CameraComponent->bUsePawnControlRotation = false;


    PlayerCameraComponent =
        CreateDefaultSubobject<UPlayerCameraComponent>(
            TEXT("PlayerCameraComponent"));


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

    KnockbackComponent =
        CreateDefaultSubobject<UKnockbackComponent>(
            TEXT("KnockbackComponent"));


    // -------------------------------------------------------------------------
    // Tick
    //
    // Character has NO actor tick now. All components manage their own on-demand ticks.
    // -------------------------------------------------------------------------

    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;
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

    if (PlayerCameraComponent && SpringArmComponent && CameraComponent)
    {
        PlayerCameraComponent->SetupCameraReferences(
            SpringArmComponent,
            CameraComponent);
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


void APlayerCharacter::Move(
    const FInputActionValue& Value)
{
    const FVector2D MovementVector =
        Value.Get<FVector2D>();

    if (!Controller)
    {
        return;
    }

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
    if (PlayerCameraComponent)
    {
        PlayerCameraComponent->HandleZoom(
            Value.Get<float>());
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

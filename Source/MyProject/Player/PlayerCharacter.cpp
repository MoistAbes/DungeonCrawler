#include "PlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Environment/Kinetic/Components/KnockbackComponent/KnockbackComponent.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/InteractionComponent/InteractionComponent.h"
#include "MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Player/Components/PlayerCameraComponent/PlayerCameraComponent.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"


APlayerCharacter::APlayerCharacter()
{
    /*
     * ACharacter already provides:
     * - RootComponent = CapsuleComponent
     * - GetMesh()     = SkeletalMeshComponent
     * - GetCharacterMovement() = CharacterMovementComponent
     */

    bReplicates = true;

    // -------------------------------------------------------------------------
    // Capsule Component (Collision)
    // -------------------------------------------------------------------------

    GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
    GetCapsuleComponent()->SetCollisionProfileName(
        UCollisionProfile::Pawn_ProfileName);
    GetCapsuleComponent()->SetHiddenInGame(false);


    // -------------------------------------------------------------------------
    // Skeletal Mesh
    // -------------------------------------------------------------------------

    if (GetMesh())
    {
        GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
        GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
    }


    // -------------------------------------------------------------------------
    // Character Movement
    // -------------------------------------------------------------------------

    UCharacterMovementComponent* MoveComp =
        GetCharacterMovement();

    if (MoveComp)
    {
        MoveComp->bOrientRotationToMovement = true;
        MoveComp->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

        MoveComp->JumpZVelocity = 600.0f;
        MoveComp->AirControl    = 0.2f;

        MoveComp->MaxWalkSpeed = 600.0f;

        /*
         * Enable physics interaction in CharacterMovementComponent.
         * We keep the engine system enabled because it provides useful
         * built-in behaviors (like push downward on standing).
         *
         * However, for directional pushing we intercept the event in
         * MoveBlockedBy and calculate the physical impulse ourselves.
         */
        MoveComp->bEnablePhysicsInteraction = true;
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
    // Hold Anchor
    // -------------------------------------------------------------------------

    HoldAnchorComponent =
        CreateDefaultSubobject<USceneComponent>(
            TEXT("HoldAnchorComponent"));

    HoldAnchorComponent->SetupAttachment(
        GetCapsuleComponent());

    HoldAnchorComponent->SetRelativeLocation(
        FVector(100.0f, 0.0f, BaseEyeHeightOffset - 15.0f));


    // -------------------------------------------------------------------------
    // Gameplay Components
    // -------------------------------------------------------------------------

    InteractionComponent =
        CreateDefaultSubobject<UInteractionComponent>(
            TEXT("InteractionComponent"));

    PhysicsCarryComponent =
        CreateDefaultSubobject<UPhysicsCarryComponent>(
            TEXT("PhysicsCarryComponent"));

    DamageableComponent =
        CreateDefaultSubobject<UDamageableComponent>(
            TEXT("DamageableComponent"));

    KnockbackComponent =
        CreateDefaultSubobject<UKnockbackComponent>(
            TEXT("KnockbackComponent"));

    StatusEffectComponent =
        CreateDefaultSubobject<UStatusEffectComponent>(
            TEXT("StatusEffectComponent"));


    // -------------------------------------------------------------------------
    // Tick
    // -------------------------------------------------------------------------

    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;
}


EPhysicalMaterialType
APlayerCharacter::GetMaterialType_Implementation() const
{
    return MaterialType;
}


void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (const APlayerController* PC =
        Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
                PC->GetLocalPlayer()))
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
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EnhancedInputComponent =
        Cast<UEnhancedInputComponent>(PlayerInputComponent);

    if (!EnhancedInputComponent)
    {
        return;
    }


    // -------------------------------------------------------------------------
    // Move
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
    // Interact (Klawisz E)
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
    // Throw (Klawisz R / LPM)
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
    }
}


void APlayerCharacter::MoveBlockedBy(
    const FHitResult& Impact)
{
    Super::MoveBlockedBy(Impact);

    UPrimitiveComponent* HitComp = Impact.GetComponent();
    if (!HitComp || !HitComp->IsSimulatingPhysics())
    {
        return;
    }

    /*
     * We calculate push physics using the kinetic library.
     * The library will:
     * 1. Validate the component
     * 2. Check if the mass is within pushable limits
     * 3. Apply the force with proper physics damping
     *
     * We use the character forward vector as push direction.
     */
    UKineticForceLibrary::TryApplyPhysicsPush(
        HitComp,
        Impact,
        GetActorForwardVector(),
        PlayerPushForce,
        MaxPushableMass);
}


void APlayerCharacter::Move(
    const FInputActionValue& Value)
{
    const FVector2D MovementVector =
        Value.Get<FVector2D>();

    if (Controller == nullptr)
    {
        return;
    }

    const FRotator Rotation =
        Controller->GetControlRotation();

    const FRotator YawRotation(
        0.0f,
        Rotation.Yaw,
        0.0f);

    const FVector ForwardDirection =
        FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

    const FVector RightDirection =
        FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

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

    if (Controller == nullptr)
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
    // 1. Jeśli postać już coś trzyma w rękach -> E oznacza upuszczenie / rzut zamachem
    if (PhysicsCarryComponent && PhysicsCarryComponent->IsCarrying())
    {
        PhysicsCarryComponent->DropOrSwing();
        return;
    }

    // 2. Jeśli mamy wolne ręce -> badamy celownik przez InteractionComponent
    if (!InteractionComponent)
    {
        return;
    }

    FHitResult HitResult;
    if (InteractionComponent->PerformTrace(HitResult))
    {
        AActor* HitActor = HitResult.GetActor();
        if (!HitActor) return;

        // Priorytet A: Obiekt fizyczny do podniesienia (IGrabbable)
        if (HitActor->Implements<UGrabbableInterface>())
        {
            if (PhysicsCarryComponent)
            {
                PhysicsCarryComponent->TryGrab(HitActor, HitResult.GetComponent());
            }
            return;
        }

        // Priorytet B: Logiczny mechanizm lochu (IInteractable) - dźwignia, przełącznik
        if (HitActor->Implements<UInteractableInterface>())
        {
            InteractionComponent->InteractWith(HitActor);
            return;
        }
    }
}


void APlayerCharacter::HandleThrow()
{
    if (PhysicsCarryComponent)
    {
        PhysicsCarryComponent->ThrowCurrentProp();
    }
}

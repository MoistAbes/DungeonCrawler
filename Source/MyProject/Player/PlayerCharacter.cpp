#include "PlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/UserWidget.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Interaction/Components/InteractionComponent/InteractionComponent.h"
#include "MyProject/UI/PlayerHUDWidget/PlayerHUDWidget.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

APlayerCharacter::APlayerCharacter()
{
    // 1. Główna bryła fizyczna (Root) - Całkowita wysokość 180 cm (HalfHeight = 90 cm)
    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionCylinder"));
    RootComponent = CapsuleComponent;
    CapsuleComponent->InitCapsuleSize(35.0f, 90.0f);
    CapsuleComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Block);
    CapsuleComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    CapsuleComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    CapsuleComponent->SetSimulatePhysics(true);
    CapsuleComponent->SetNotifyRigidBodyCollision(true);
    CapsuleComponent->SetUseCCD(true);
    CapsuleComponent->SetMassOverrideInKg(NAME_None, 80.0f, true);
    
    // Wizualizacja bryły w celach prototypowania
    CapsuleComponent->SetHiddenInGame(false);
    CapsuleComponent->SetVisibility(true);

    // Naturalny opór powietrza (Z grawitacji nie jest tłumione) i blokada przechyłów
    CapsuleComponent->SetLinearDamping(0.01f);
    CapsuleComponent->SetAngularDamping(100.0f);
    CapsuleComponent->BodyInstance.bLockXRotation = true;
    CapsuleComponent->BodyInstance.bLockYRotation = true;
    CapsuleComponent->BodyInstance.bLockZRotation = true;

    // 2. Siatka szkieletowa postaci (Ukryta podczas prototypowania bryły)
    MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(CapsuleComponent);
    MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
    MeshComponent->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
    MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
    MeshComponent->SetGenerateOverlapEvents(false);
    MeshComponent->SetHiddenInGame(true);
    MeshComponent->SetVisibility(false);

    // 3. System kamery TPP/FPP - punkt zaczepienia na poziomie oczu/głowy (+65 cm od środka)
    SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArmComponent->SetupAttachment(CapsuleComponent);
    SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeightOffset));
    SpringArmComponent->bUsePawnControlRotation = true;
    TargetArmLength = 400.0f;
    SpringArmComponent->TargetArmLength = TargetArmLength;

    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
    CameraComponent->bUsePawnControlRotation = false;

    // 4. Komponenty domenowe
    PhysicsHandleComponent = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandleComponent"));
    PhysicsHandleComponent->LinearDamping = 200.0f;
    PhysicsHandleComponent->LinearStiffness = 1500.0f;
    PhysicsHandleComponent->AngularDamping = 200.0f;
    PhysicsHandleComponent->AngularStiffness = 1500.0f;
    PhysicsHandleComponent->InterpolationSpeed = 50.0f;
    PhysicsHandleComponent->bInterpolateTarget = true;

    InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
    DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));

    // Optymalizacja CPU: wyłączony stały Tick (100% Event-Driven)
    PrimaryActorTick.bCanEverTick = true;
    SetActorTickEnabled(false);
    bIsZooming = false;
}

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (CapsuleComponent)
    {
        // Wymuszenie rozmiaru 180 cm, pełnej fizyki i zerowego tłumienia grawitacji
        CapsuleComponent->SetCapsuleSize(35.0f, 90.0f);
        CapsuleComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Block);
        CapsuleComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        CapsuleComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        CapsuleComponent->SetSimulatePhysics(true);
        CapsuleComponent->SetHiddenInGame(false);
        CapsuleComponent->SetVisibility(true);
        CapsuleComponent->SetLinearDamping(0.01f);
        CapsuleComponent->SetAngularDamping(100.0f);
        CapsuleComponent->BodyInstance.bLockXRotation = true;
        CapsuleComponent->BodyInstance.bLockYRotation = true;
        CapsuleComponent->BodyInstance.bLockZRotation = true;
        CapsuleComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        CapsuleComponent->RecreatePhysicsState();
        CapsuleComponent->OnComponentHit.AddDynamic(this, &APlayerCharacter::HandleCapsuleHit);
        CapsuleComponent->WakeRigidBody();
    }

    if (SpringArmComponent)
    {
        // Ustawienie punktu zaczepienia ramienia kamery na wysokości oczu postaci
        SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeightOffset));
    }

    if (MeshComponent)
    {
        // Ukrycie siatki zastępczej na rzecz widocznej bryły kolizyjnej
        MeshComponent->SetHiddenInGame(true);
        MeshComponent->SetVisibility(false);
    }

    // Inicjalizacja kontekstu Enhanced Input
    if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }

        // Inicjalizacja interfejsu HUD
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

void APlayerCharacter::Move(const FInputActionValue& Value)
{
    // Blokada napędu w locie – zachowanie czystej bezwładności i pędu fizycznego
    if (!IsGrounded())
    {
        return;
    }

    const FVector2D MovementVector = Value.Get<FVector2D>();

    if (Controller != nullptr && CapsuleComponent != nullptr)
    {
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawRotation(0, Rotation.Yaw, 0);

        const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        const FVector MoveDir = (ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X).GetSafeNormal();

        if (!MoveDir.IsNearlyZero())
        {
            // Różnicowy wektor siły przyspieszenia: F = (V_target - V_current) * K
            const FVector TargetVelocity = MoveDir * MaxWalkSpeed;
            const FVector CurrentVelocity = CapsuleComponent->GetPhysicsLinearVelocity();
            const FVector CurrentVelocity2D(CurrentVelocity.X, CurrentVelocity.Y, 0.0f);

            const FVector VelocityError = TargetVelocity - CurrentVelocity2D;
            const FVector DriveForce = VelocityError * AccelerationResponsiveness;

            CapsuleComponent->AddForce(DriveForce, NAME_None, true);
        }
    }
}

void APlayerCharacter::MoveCompleted()
{
    if (CapsuleComponent && IsGrounded())
    {
        // Natychmiastowe zatrzymanie ruchu poziomego (XY), oś Z i grawitacja pozostają w 100% nietknięte
        FVector CurrentVelocity = CapsuleComponent->GetPhysicsLinearVelocity();
        CurrentVelocity.X = 0.0f;
        CurrentVelocity.Y = 0.0f;
        CapsuleComponent->SetPhysicsLinearVelocity(CurrentVelocity);
    }
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
        SetActorTickEnabled(true);
    }
}

void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    const float CurrentLength = SpringArmComponent->TargetArmLength;

    if (!FMath::IsNearlyEqual(CurrentLength, TargetArmLength, 0.1f))
    {
        const float NewLength = FMath::FInterpTo(CurrentLength, TargetArmLength, DeltaTime, 15.0f);
        SpringArmComponent->TargetArmLength = NewLength;
    }
    else
    {
        SpringArmComponent->TargetArmLength = TargetArmLength;
        bIsZooming = false;
        SetActorTickEnabled(false);
    }
}

void APlayerCharacter::Jump()
{
    if (CapsuleComponent && IsGrounded())
    {
        // Skok impulsowy w osi Z
        FVector Vel = CapsuleComponent->GetPhysicsLinearVelocity();
        Vel.Z = 0.0f;
        CapsuleComponent->SetPhysicsLinearVelocity(Vel);

        CapsuleComponent->AddImpulse(FVector(0.0f, 0.0f, JumpImpulseVelocity), NAME_None, true);
    }
}

bool APlayerCharacter::IsGrounded() const
{
    if (!CapsuleComponent || !GetWorld()) return false;

    const float Radius = CapsuleComponent->GetScaledCapsuleRadius();
    const float HalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();

    // Start na środku dolnej półkuli kapsuły
    const FVector Start = CapsuleComponent->GetComponentLocation() - FVector(0.0f, 0.0f, HalfHeight - Radius);
    const FVector End = Start - FVector(0.0f, 0.0f, GroundCheckDistance + 5.0f);

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);

    FHitResult HitResult;
    // Kulisty Sweep (Sphere Sweep) obejmujący pełny spód kapsuły
    const FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius * 0.9f);
    const bool bHit = GetWorld()->SweepSingleByChannel(
        HitResult,
        Start,
        End,
        FQuat::Identity,
        ECC_Visibility,
        SphereShape,
        QueryParams
    );

    // Warunek: obiekt blokujący oraz normalna podłoża skierowana ku górze (Z > 0.5)
    return bHit && HitResult.bBlockingHit && (HitResult.ImpactNormal.Z > 0.5f);
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

void APlayerCharacter::HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                       UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                       const FHitResult& Hit)
{
    if (!OtherActor || OtherActor == this || !CapsuleComponent) return;

    const FVector PlayerVelocity = CapsuleComponent->GetPhysicsLinearVelocity();
    FVector OtherVelocity = FVector::ZeroVector;

    if (OtherComp && OtherComp->IsSimulatingPhysics())
    {
        OtherVelocity = OtherComp->GetPhysicsLinearVelocity();
    }
    else if (OtherActor)
    {
        OtherVelocity = OtherActor->GetVelocity();
    }

    const FVector RelativeVelocity = PlayerVelocity - OtherVelocity;
    const float ImpactSpeed = FMath::Abs(FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal));

    if (DamageableComponent)
    {
        DamageableComponent->ApplyKineticImpact(ImpactSpeed);
    }
}

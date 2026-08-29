#include "PlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "../Components/Health/UHealthComponent.h"
#include "../UI/PlayerHUDWidget/PlayerHUDWidget.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "../Interaction/InteractionComponent/InteractionComponent.h"

APlayerCharacter::APlayerCharacter()
{
    // W konstruktorze:
    PhysicsHandleComponent = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandleComponent"));
    PhysicsHandleComponent->LinearDamping = 200.0f;
    PhysicsHandleComponent->LinearStiffness = 1500.0f;
    PhysicsHandleComponent->AngularDamping = 200.0f;
    PhysicsHandleComponent->AngularStiffness = 1500.0f;
    PhysicsHandleComponent->InterpolationSpeed = 50.0f;
    PhysicsHandleComponent->bInterpolateTarget = true;
    
    InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
    
    // Optymalizacja: domyślnie wyłączamy globalny Tick (odpowiednik braku aktywnego wątku w tle)
    PrimaryActorTick.bCanEverTick = true;
    SetActorTickEnabled(false); 
    bIsZooming = false;
    
    // Tworzenie instancji serwisu wewnątrz kontenera (Dependency Injection w konstruktorze)
    HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

    SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArmComponent->SetupAttachment(RootComponent);
    SpringArmComponent->bUsePawnControlRotation = true;

    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
    CameraComponent->bUsePawnControlRotation = false;

    TargetArmLength = 400.0f;
    SpringArmComponent->TargetArmLength = TargetArmLength;
}

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
        
        // Inicjalizacja i wstrzyknięcie zależności do warstwy UI (View Initialization)
        if (HUDWidgetClass && PlayerController->IsLocalController())
        {
            ActiveHUDWidget = CreateWidget<UPlayerHUDWidget>(PlayerController, HUDWidgetClass);
            if (ActiveHUDWidget)
            {
                ActiveHUDWidget->AddToViewport();
                ActiveHUDWidget->BindHealthComponent(HealthComponent);
            }
        }
    }
    
    // Weryfikacja stanu początkowego przy starcie
    if (HealthComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Domain|Player] Initial Health initialized: %.1f"), HealthComponent->GetCurrentValue());
    }
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
        EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
        EnhancedInputComponent->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Zoom);
        
        if (InteractAction)
        {
            EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleInteract);
        }
        if (ThrowAction)
        {
            EnhancedInputComponent->BindAction(ThrowAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleThrow);
        }
    }
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
    FVector2D MovementVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawRotation(0, Rotation.Yaw, 0);

        const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        AddMovementInput(ForwardDirection, MovementVector.Y);
        AddMovementInput(RightDirection, MovementVector.X);
    }
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
    FVector2D LookAxisVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        AddControllerYawInput(LookAxisVector.X);
        AddControllerPitchInput(LookAxisVector.Y);
    }
}

void APlayerCharacter::Zoom(const FInputActionValue& Value)
{
    // Transformer: Odwrócenie osi wejściowej scrolla
    float ZoomValue = Value.Get<float>() * -1.0f;
    
    // Ustawienie nowego stanu docelowego w granicach biznesowych (Clamp)
    float OldTarget = TargetArmLength;
    TargetArmLength = FMath::Clamp(TargetArmLength + (ZoomValue * ZoomStep), 0.0f, MaxZoomLength);

    UE_LOG(LogTemp, Warning, TEXT("[Controller/Input] Raw: %f | Inverted: %f | Old Target: %f | New Target: %f"), 
        Value.Get<float>(), ZoomValue, OldTarget, TargetArmLength);

    // Aktywacja cyklu renderowania wyłącznie na żądanie (Lazy Activation)
    if (!bIsZooming)
    {
        bIsZooming = true;
        SetActorTickEnabled(true);
        UE_LOG(LogTemp, Warning, TEXT("[Lifecycle] Zoom started. Actor Tick enabled."));
    }
}

void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    float CurrentLength = SpringArmComponent->TargetArmLength;

    // Płynna interpolacja stanu fizycznego do wartości docelowej
    if (!FMath::IsNearlyEqual(CurrentLength, TargetArmLength, 0.1f))
    {
        float NewLength = FMath::FInterpTo(CurrentLength, TargetArmLength, DeltaTime, 15.0f);
        SpringArmComponent->TargetArmLength = NewLength;
        GetMesh()->SetHiddenInGame(NewLength <= FppThreshold);

        UE_LOG(LogTemp, Verbose, TEXT("[Service/Interpolation] Current: %f | Target: %f | DeltaTime: %f"), 
            NewLength, TargetArmLength, DeltaTime);
    }
    else
    {
        // Osiągnięto cel biznesowy – zamknięcie pętli i zwolnienie zasobów procesora (Deactivation)
        SpringArmComponent->TargetArmLength = TargetArmLength;
        GetMesh()->SetHiddenInGame(TargetArmLength <= FppThreshold);
        
        bIsZooming = false;
        SetActorTickEnabled(false);
        UE_LOG(LogTemp, Warning, TEXT("[Lifecycle] Target reached (%f). Actor Tick disabled (Lazy Deactivation)."), TargetArmLength);
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
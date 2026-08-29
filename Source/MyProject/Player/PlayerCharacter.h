#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlayerCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UPhysicsHandleComponent;
class UInteractionComponent;
class UCameraComponent;
class USpringArmComponent;
class UHealthComponent;
class UPlayerHUDWidget;
struct FInputActionValue;

UCLASS(Abstract)
class MYPROJECT_API APlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APlayerCharacter();

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
    virtual void Tick(float DeltaTime) override;

    // --- Domenowe Komponenty Podrzędne ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UPhysicsHandleComponent> PhysicsHandleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UInteractionComponent> InteractionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UHealthComponent> HealthComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> SpringArmComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> CameraComponent;

    // --- Enhanced Input: Konfiguracja Mapowania i Akcji ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Mapping")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> ZoomAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> InteractAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> ThrowAction;

    // --- UI Configuration ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Configuration")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

private:
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.0", ClampMax = "800.0"))
    float MaxZoomLength = 400.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float FppThreshold = 15.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom")
    float ZoomStep = 50.0f;

    UPROPERTY()
    TObjectPtr<UPlayerHUDWidget> ActiveHUDWidget;

    float TargetArmLength = 400.0f;
    bool bIsZooming = false;

    // Handlery wejścia (Controller Action Listeners)
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Zoom(const FInputActionValue& Value);
    void HandleInteract();
    void HandleThrow();
};
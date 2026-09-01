#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlayerCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UPhysicsHandleComponent;
class UCameraComponent;
class USpringArmComponent;
class UInteractionComponent;
class UDamageableComponent;
class UPlayerHUDWidget;
struct FInputActionValue;

UCLASS(Abstract)
class MYPROJECT_API APlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APlayerCharacter();

    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UPhysicsHandleComponent> PhysicsHandleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UInteractionComponent> InteractionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDamageableComponent> DamageableComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpringArmComponent> SpringArmComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> CameraComponent;

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Player|Camera",
        meta = (ClampMin = "0.0", ClampMax = "120.0"))
    float BaseEyeHeightOffset = 65.0f;

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Player|Camera",
        meta = (ClampMin = "0.0", ClampMax = "1200.0"))
    float MaxZoomLength = 400.0f;

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Player|Camera",
        meta = (ClampMin = "0.0"))
    float ZoomStep = 50.0f;

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Player|Camera",
        meta = (ClampMin = "0.0"))
    float ZoomInterpSpeed = 15.0f;

    // -------------------------------------------------------------------------
    // Enhanced Input
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputAction> ZoomAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputAction> InteractAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputAction> ThrowAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input")
    TObjectPtr<UInputAction> JumpAction;

    // -------------------------------------------------------------------------
    // HUD
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

protected:
    virtual void BeginPlay() override;

    virtual void SetupPlayerInputComponent(
        UInputComponent* PlayerInputComponent) override;

    virtual void Tick(float DeltaTime) override;

private:
    // -------------------------------------------------------------------------
    // Input
    // -------------------------------------------------------------------------

    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Zoom(const FInputActionValue& Value);

    void HandleInteract();
    void HandleThrow();

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------

    void UpdateZoomTickState();

    // -------------------------------------------------------------------------
    // Runtime state
    // -------------------------------------------------------------------------

    float TargetArmLength = 400.0f;
    bool bIsZooming = false;

    UPROPERTY()
    TObjectPtr<UPlayerHUDWidget> ActiveHUDWidget;
};
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlayerCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UPhysicsHandleComponent;
class UCameraComponent;
class USpringArmComponent;
class UPlayerCameraComponent;
class UInteractionComponent;
class UDamageableComponent;
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
    TObjectPtr<UPlayerCameraComponent> PlayerCameraComponent;

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
    // Camera Settings
    // -------------------------------------------------------------------------

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Player|Camera",
        meta = (ClampMin = "0.0", ClampMax = "120.0"))
    float BaseEyeHeightOffset = 65.0f;

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

protected:
    virtual void BeginPlay() override;

    virtual void SetupPlayerInputComponent(
        UInputComponent* PlayerInputComponent) override;

private:
    // -------------------------------------------------------------------------
    // Input Handlers
    // -------------------------------------------------------------------------

    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Zoom(const FInputActionValue& Value);

    void HandleInteract();
    void HandleThrow();
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PlayerCharacter.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
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
class MYPROJECT_API APlayerCharacter : public APawn
{
    GENERATED_BODY()

public:
    APlayerCharacter();

    UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
    USkeletalMeshComponent* GetMesh() const { return MeshComponent; }
    UCameraComponent* GetCameraComponent() const { return CameraComponent; }
    USpringArmComponent* GetSpringArmComponent() const { return SpringArmComponent; }

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
    virtual void Tick(float DeltaTime) override;

    // Sprawdzenie kontaktu z podłożem (Sphere Sweep)
    bool IsGrounded() const;

    // Hook bezpośrednich zderzeń kapsuły ze światem / bryłami fizycznymi
    UFUNCTION()
    virtual void HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                 UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                 const FHitResult& Hit);

    // --- Domenowe Komponenty Podrzędne (IoC / Dependency Injection) ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCapsuleComponent> CapsuleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USkeletalMeshComponent> MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UPhysicsHandleComponent> PhysicsHandleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UInteractionComponent> InteractionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDamageableComponent> DamageableComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> SpringArmComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> CameraComponent;

    // Wysokość punktu zaczepienia kamery (poziom oczu/głowy) względem środka kapsuły
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "90.0"))
    float BaseEyeHeightOffset = 65.0f;

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
    
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> JumpAction;

    // --- UI Configuration ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Configuration")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

    // --- Parametry Ruchu Fizycznego ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PhysicsMovement|Speed", meta = (ClampMin = "0.0"))
    float MaxWalkSpeed = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PhysicsMovement|Speed", meta = (ClampMin = "0.0"))
    float AccelerationResponsiveness = 18.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PhysicsMovement|Jump", meta = (ClampMin = "0.0"))
    float JumpImpulseVelocity = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PhysicsMovement|GroundCheck", meta = (ClampMin = "0.0"))
    float GroundCheckDistance = 15.0f;

private:
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.0", ClampMax = "800.0"))
    float MaxZoomLength = 400.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom")
    float ZoomStep = 50.0f;

    UPROPERTY()
    TObjectPtr<UPlayerHUDWidget> ActiveHUDWidget;

    float TargetArmLength = 400.0f;
    bool bIsZooming = false;

    // Handlery wejścia (Controller Action Listeners)
    void Move(const FInputActionValue& Value);
    void MoveCompleted();
    void Look(const FInputActionValue& Value);
    void Zoom(const FInputActionValue& Value);
    void Jump();
    void HandleInteract();
    void HandleThrow();
};

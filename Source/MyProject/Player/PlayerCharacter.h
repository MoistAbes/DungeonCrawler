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

/**
 * Główna klasa postaci gracza sterowana w architekturze kinematycznej (Split-Axis Movement).
 * Zapewnia stabilny ruch, skok, odrzut (Knockback) oraz fizyczne pchanie propów.
 */
UCLASS(Abstract)
class MYPROJECT_API APlayerCharacter : public APawn
{
    GENERATED_BODY()

public:
    APlayerCharacter();

    // --- Gettery Komponentów ---
    UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
    USkeletalMeshComponent* GetMesh() const { return MeshComponent; }
    UCameraComponent* GetCameraComponent() const { return CameraComponent; }
    USpringArmComponent* GetSpringArmComponent() const { return SpringArmComponent; }

    // Bieżący wektor prędkości (używany m.in. przez system animacji)
    virtual FVector GetVelocity() const override { return CurrentVelocity + KnockbackVelocity; }

    // Sprawdzenie czy postać stoi na stabilnym podłożu
    UFUNCTION(BlueprintPure, Category = "Player|Movement")
    bool IsGrounded() const { return bIsGrounded; }

    // Ujednolicony system odrzutu bojowego
    UFUNCTION(BlueprintCallable, Category = "Player|Combat")
    void ApplyKnockback(const FVector& Impulse);

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
    virtual void Tick(float DeltaTime) override;

    // --- Komponenty Podstawowe (IoC / Dependency Injection) ---
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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpringArmComponent> SpringArmComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> CameraComponent;

    // --- Konfiguracja Kamery ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Camera", meta = (ClampMin = "0.0", ClampMax = "120.0"))
    float BaseEyeHeightOffset = 65.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Camera", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
    float MaxZoomLength = 400.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Camera")
    float ZoomStep = 50.0f;

    // --- Enhanced Input ---
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

    // --- Interfejs Gracza (HUD) ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

    // --- Parametry Kinematycznego Ruchu ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Speed", meta = (ClampMin = "0.0"))
    float MaxWalkSpeed = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Speed", meta = (ClampMin = "0.0"))
    float AccelerationResponsiveness = 18.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Speed", meta = (ClampMin = "0.0"))
    float DecelerationResponsiveness = 25.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Jump", meta = (ClampMin = "0.0"))
    float JumpImpulseVelocity = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Gravity")
    float GravityScale = 1.8f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|GroundCheck", meta = (ClampMin = "0.0"))
    float GroundCheckDistance = 15.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|GroundCheck", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WalkableFloorZ = 0.7f;

    // --- Popychanie Propów Fizycznych ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Props", meta = (ClampMin = "0.0"))
    float PushablePropMassThreshold = 100.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Props", meta = (ClampMin = "0.0"))
    float BasePushImpulse = 400.0f;

    // --- Parametry Odrzutu (Knockback) ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Knockback", meta = (ClampMin = "0.0"))
    float KnockbackDamping = 6.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Movement|Knockback", meta = (ClampMin = "0.0"))
    float KnockbackDamageImpactThreshold = 500.0f;

private:
    // Stan ruchu
    FVector CurrentVelocity = FVector::ZeroVector;
    FVector DesiredMoveDirection = FVector::ZeroVector;
    FVector KnockbackVelocity = FVector::ZeroVector;
    bool bIsGrounded = false;
    bool bMovementInputActive = false;
    bool bIsKnockedBack = false;

    // Stan kamery
    float TargetArmLength = 400.0f;
    bool bIsZooming = false;

    // Śledzenie ruchomych platform (Base Tracking)
    TWeakObjectPtr<UPrimitiveComponent> CurrentBaseComponent;
    FVector PreviousBaseLocation = FVector::ZeroVector;
    FRotator PreviousBaseRotation = FRotator::ZeroRotator;

    UPROPERTY()
    TObjectPtr<UPlayerHUDWidget> ActiveHUDWidget;

    // Zarządzanie stanem uśpienia (On-Demand Tick: 0ms CPU w spoczynku)
    void UpdateTickState();

    // Wewnętrzne metody kinematyki
    bool PerformGroundCheck(FHitResult& OutHitResult);
    void PerformMovement(float DeltaTime);
    void UpdateBaseTracking(float DeltaTime);
    void HandlePropSweepHit(const FHitResult& Hit, float DeltaTime);

    // Handlery wejścia Enhanced Input
    void Move(const FInputActionValue& Value);
    void MoveCompleted();
    void Look(const FInputActionValue& Value);
    void Zoom(const FInputActionValue& Value);
    void Jump();
    void HandleInteract();
    void HandleThrow();
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "PlayerCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UPhysicsHandleComponent;
class UCameraComponent;
class USpringArmComponent;
class UPlayerCameraComponent;
class UInteractionComponent;
class UDamageableComponent;
class UKnockbackComponent;
class UStatusEffectComponent;
struct FInputActionValue;

UCLASS(Abstract)
class MYPROJECT_API APlayerCharacter : public ACharacter, public IMaterialProviderInterface
{
    GENERATED_BODY()

public:
    APlayerCharacter();

    // --- IMaterialProviderInterface ---
    virtual EPhysicalMaterialType GetMaterialType_Implementation() const override;

    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    /** Komponent sterujący płynnym zoomem i zachowaniem kamery */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UPlayerCameraComponent> PlayerCameraComponent;

    /** Fizyczny uchwyt umożliwiający chwytanie i niesienie obiektów przed postacią */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UPhysicsHandleComponent> PhysicsHandleComponent;

    /** Komponent odpowiedzialny za detekcję i logikę interakcji z propami i przełącznikami */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UInteractionComponent> InteractionComponent;

    /** Komponent zarządzający punktami zdrowia i uszkodzeniami fizycznymi gracza */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UDamageableComponent> DamageableComponent;

    /** Komponent odbierający siły kinetyczne i odrzuty postaci */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UKnockbackComponent> KnockbackComponent;

    /** Komponent obsługujący stany żywiołowe gracza (np. podpalenie) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UStatusEffectComponent> StatusEffectComponent;

    /** Ramię kamery stabilizujące perspektywę trzecioosobową */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<USpringArmComponent> SpringArmComponent;

    /** Główna kamera widoku gracza */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UCameraComponent> CameraComponent;

    // -------------------------------------------------------------------------
    // Camera Settings
    // -------------------------------------------------------------------------

    /** Bazowa wysokość punktu widzenia oczu postaci względem kapsuły */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Camera",
        meta = (ClampMin = "0.0", ClampMax = "120.0"))
    float BaseEyeHeightOffset = 65.0f;

    // -------------------------------------------------------------------------
    // Enhanced Input
    // -------------------------------------------------------------------------

    /** Domyślny kontekst mapowania klawiszy wejściowych dla gracza */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    /** Akcja poruszania się (WASD / gałka analogowa) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputAction> MoveAction;

    /** Akcja rozglądania się (Mysz / gałka analogowa) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputAction> LookAction;

    /** Akcja przybliżania i oddalania kamery (Kółko myszy) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputAction> ZoomAction;

    /** Akcja wejścia w interakcję lub podniesienia/upuszczenia przedmiotu (Klawisz E) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputAction> InteractAction;

    /** Akcja rzutu aktualnie trzymanym przedmiotem (LPM) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputAction> ThrowAction;

    /** Akcja skoku postaci (Spacja) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Input")
    TObjectPtr<UInputAction> JumpAction;

    // -------------------------------------------------------------------------
    // Physics Prop Pushing (Opcja B: Kontrolowane pchanie fizyczne z zachowaniem praw pędu)
    // -------------------------------------------------------------------------

    /** Maksymalna masa pojedynczego propa (w kg), którą postać podejmuje próbę przepchnąć */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|PhysicsPush", meta = (ClampMin = "0.0"))
    float MaxPushableMass = 100.0f;

    /** Siła pchania gracza w niutonach (N). Determinuje fizyczny wydatek siły na propa/układ propów */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|PhysicsPush", meta = (ClampMin = "0.0"))
    float PlayerPushForce = 150000.0f;

protected:
    virtual void BeginPlay() override;

    virtual void SetupPlayerInputComponent(
        UInputComponent* PlayerInputComponent) override;

    virtual void MoveBlockedBy(const FHitResult& Impact) override;

    // -------------------------------------------------------------------------
    // Material Configuration
    // -------------------------------------------------------------------------

    /** Tożsamość materiałowa postaci (Flesh) determinująca reakcje chemiczne i obrażenia */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
    EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Flesh;

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

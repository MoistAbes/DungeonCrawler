#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerCameraComponent.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 * Komponent odpowiedzialny za logikę kamery gracza (płynny zoom, dystans, ramię kamery).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UPlayerCameraComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPlayerCameraComponent();

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /** Wywołaj przy wejściu z osi zoomu (np. Mouse Wheel) */
    void HandleZoom(float InputValue);

    /** Ustawia referencje do komponentów ramienia i kamery */
    void SetupCameraReferences(
        USpringArmComponent* InSpringArm,
        UCameraComponent* InCamera);

    float GetTargetArmLength() const { return TargetArmLength; }

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Parametry konfiguracyjne
    // -------------------------------------------------------------------------

    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Player|Camera",
        meta = (ClampMin = "0.0", ClampMax = "1200.0"))
    float MinZoomLength = 0.0f;

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

private:
    UPROPERTY()
    TObjectPtr<USpringArmComponent> SpringArm;

    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;

    float TargetArmLength = 400.0f;
    bool bIsZooming = false;

    void UpdateTickState();
};

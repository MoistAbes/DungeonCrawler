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

    /** Minimalna długość ramienia kamery przy maksymalnym przybliżeniu (0 = widok z pierwszej osoby) */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Camera",
        meta = (ClampMin = "0.0", ClampMax = "1200.0"))
    float MinZoomLength = 0.0f;

    /** Maksymalna długość ramienia kamery przy maksymalnym oddaleniu */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Camera",
        meta = (ClampMin = "0.0", ClampMax = "1200.0"))
    float MaxZoomLength = 400.0f;

    /** Krok zmiany długości ramienia przy pojedynczym obrocie kółka myszy */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Camera",
        meta = (ClampMin = "0.0"))
    float ZoomStep = 50.0f;

    /** Szybkość płynnej interpolacji kamery do docelowej odległości */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Custom|Camera",
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

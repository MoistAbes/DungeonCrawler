#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

/**
 * Serwis domenowy odpowiedzialny za wykrywanie, chwytanie i rzucanie obiektów fizycznych
 * oraz interakcję logiczną (przełączniki, dźwignie, mechanizmy).
 * W pełni zsynchronizowany w sieci: Client-Request -> Server-Authoritative Execution.
 * Wykorzystuje dynamiczny, kinematyczny ruch ze sweepem zapobiegający efektowi spychacza (bulldozer)
 * oraz czystą prędkość kątową kamery do rzutu zamachem myszką (Camera Angular Swing Throw).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Główna akcja: podnieś lub upuść obiekt pod celownikiem / wektorem wzroku */
    void PrimaryInteract();

    /** Dedykowana akcja rzutu na wprost (LPM) */
    void ThrowCurrentProp();

    /** Powiadamia komponent o podpięciu lub odpięciu niesionego propa (dla włączenia/wyłączenia On-Demand Tick) */
    void NotifyCarriedPropAttached(AActor* InProp);
    void NotifyCarriedPropDetached();

protected:
    /** Maksymalny dystans interakcji w jednostkach silnika (cm) liczony od postaci */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float TraceDistance = 300.0f;

    /** Maksymalna masa obiektu, jaką postać może unieść (kg). Cięższe obiekty nie mogą być podniesione */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float MaxCarryMass = 35.0f;

    /** Siła pędu nadawanego obiektowi przy dedykowanym rzucie na wprost (LPM) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float ThrowImpulseStrength = 1400.0f;

    /** Maksymalny dopuszczalny dystans rozciągnięcia rąk od zablokowanego propa (cm). Przekroczenie powoduje upuszczenie */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "30.0", ClampMax = "150.0"))
    float CarryBreakDistance = 70.0f;

    /** Mnożnik pędu nadawanego propowi przy rzucie zamachem myszką (klawisz E) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "0.5", ClampMax = "2.5"))
    float SwingVelocityMultiplier = 1.2f;

    /** Minimalna prędkość kątowa zamachu myszką (cm/s), by została uznana za rzut zamachem (poniżej: czyste upuszczenie pod nogi) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "100.0", ClampMax = "600.0"))
    float MinSwingSpeedToThrow = 250.0f;

    /** Maksymalna dopuszczalna prędkość rzutu zamachem (zabezpieczenie fizyki i sieci) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "500.0", ClampMax = "5000.0"))
    float MaxSwingThrowSpeed = 2200.0f;

    // --- RPCs Sieciowe (Zarządzane przez Serwer) ---

    /** Żądanie klienta do serwera o podniesienie wskazanego propa */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);

    /** Żądanie klienta do serwera o upuszczenie lub rzucenie trzymanym propem */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestReleaseOrThrow(bool bIsThrow, const FVector_NetQuantize& LaunchVelocity);

    /** Żądanie interakcji logicznej (dźwignia, przełącznik) na serwerze */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestInteract(AActor* TargetActor);

private:
    /** Aktualnie trzymany aktor */
    UPROPERTY()
    TObjectPtr<AActor> GrabbedActor;

    /** Pomocniczy wskaźnik na trzymany komponent siatki */
    UPROPERTY()
    TObjectPtr<UPrimitiveComponent> GrabbedComponent;

    /** Poprzednia rotacja kamery do precyzyjnego wyliczania prędkości kątowej zamachu myszką */
    FRotator PreviousCameraRotation = FRotator::ZeroRotator;

    /** Wyliczona prędkość kątowa zamachu myszką na promieniu trzymania propa (cm/s) */
    FVector TrackedCameraSwingVelocity = FVector::ZeroVector;

    // --- Metody pomocnicze ---
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    bool PerformTrace(FHitResult& OutHit) const;
    void ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);
    void ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity);
    void UpdateHoldAnchorTransform(float DeltaTime);
    void UpdateCarriedPropTransform(float DeltaTime);
    void ResetGrabState();
    void StopHeavyPhysicsObject(UPrimitiveComponent* Comp, float MaxPushableMass);
};

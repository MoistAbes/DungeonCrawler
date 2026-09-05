#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

/**
 * Serwis domenowy odpowiedzialny za wykrywanie, chwytanie i rzucanie obiektów fizycznych
 * oraz interakcję logiczną (przełączniki, dźwignie, mechanizmy).
 * W pełni zsynchronizowany w sieci: Client-Request -> Server-Authoritative Execution.
 * Wykorzystuje stabilny, bezlagowy wzorzec Attach-on-Grab.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

    /** Główna akcja: podnieś lub upuść obiekt pod celownikiem / wektorem wzroku */
    void PrimaryInteract();

    /** Opcjonalna akcja rzutu trzymanym obiektem */
    void ThrowCurrentProp();

protected:
    virtual void BeginPlay() override;

    /** Maksymalny dystans interakcji w jednostkach silnika (cm) liczony od postaci */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float TraceDistance = 300.0f;

    /** Maksymalna masa obiektu, jaką postać może unieść (kg). Cięższe obiekty nie mogą być podniesione */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float MaxCarryMass = 35.0f;

    /** Siła pędu nadawanego obiektowi przy rzuceniu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float ThrowImpulseStrength = 1400.0f;

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

    // --- Metody pomocnicze ---
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    bool PerformTrace(FHitResult& OutHit) const;
    void ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);
    void ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity);
};

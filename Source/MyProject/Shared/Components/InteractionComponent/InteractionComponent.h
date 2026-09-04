#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class UPhysicsHandleComponent;

/**
 * Serwis domenowy odpowiedzialny za wykrywanie, chwytanie i rzucanie obiektów fizycznych
 * oraz interakcję logiczną (przełączniki, dźwignie, mechanizmy).
 * Może być używany zarówno przez Gracza, jak i przez postacie sterowane przez AI.
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

    /** Odległość przed postacią w osi wzroku/kamery, w której zawieszony jest trzymany obiekt */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float HoldDistance = 180.0f;

    /** Siła pędu nadawanego obiektowi przy rzuceniu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float ThrowImpulseStrength = 1200.0f;

private:
    /** Referencja do fizycznego uchwytu zarządzanego przez postać */
    UPROPERTY()
    TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

    /** Aktualnie trzymany aktor */
    UPROPERTY()
    TObjectPtr<AActor> GrabbedActor;

    /** Pomocniczy wskaźnik na trzymany komponent siatki */
    UPROPERTY()
    TObjectPtr<UPrimitiveComponent> GrabbedComponent;

    // --- Metody pomocnicze ---
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    bool PerformTrace(FHitResult& OutHit) const;
    void GrabProp(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);
    void ReleaseProp();
    void UpdateHoldLocation();
};

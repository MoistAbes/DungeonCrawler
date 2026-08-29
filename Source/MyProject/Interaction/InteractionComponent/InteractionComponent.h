#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class UPhysicsHandleComponent;
class IInteractableInterface;

/**
 * Serwis domenowy odpowiedzialny za wykrywanie, chwytanie i rzucanie obiektów fizycznych.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Główna akcja: podnieś lub upuść obiekt pod celownikiem */
    void PrimaryInteract();

    /** Opcjonalna akcja rzutu trzymanym obiektem */
    void ThrowCurrentProp();

protected:
    virtual void BeginPlay() override;

    /** Maksymalny dystans interakcji w jednostkach silnika (cm) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Config")
    float TraceDistance = 300.0f;

    /** Maksymalna masa obiektu, jaką postać może unieść (kg) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Config")
    float MaxCarryMass = 35.0f;

    /** Odległość przed kamerą, w której zawieszony jest trzymany obiekt */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Config")
    float HoldDistance = 180.0f;

    /** Siła pędu przy rzuceniu trzymanego obiektu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Config")
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
    bool PerformTrace(FHitResult& OutHit) const;
    void GrabProp(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);
    void ReleaseProp();
    void UpdateHoldLocation();
};
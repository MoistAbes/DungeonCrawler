#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "InteractionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractionProgressChanged, float, Progress01);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractionCompleted, AActor*, InteractedActor);

/**
 * Lekki komponent domenowy odpowiedzialny za wykrywanie otoczenia (Sphere/Line Trace)
 * oraz wchodzenie w interakcję logiczną z aktorami lochu (IInteractableInterface).
 * Wspiera zarówno natychmiastowe kliknięcie (Instant Interact), jak i przytrzymanie (Hold / Channeling).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Wykonuje test kolizji (Sphere lub Line Trace) w kierunku patrzenia gracza */
    bool PerformTrace(FHitResult& OutHit) const;

    /** Rozpoczyna próbę interakcji ze wskazanym aktorem (lub celem pod celownikiem) */
    void InteractWith(AActor* TargetActor);

    /** Natychmiastowe kliknięcie interakcji z aktualnym obiektem pod celownikiem */
    void TriggerInstantInteraction();

    /** Rozpoczęcie przytrzymania (Hold/Channeling) */
    void StartHoldingInteraction();

    /** Zakończenie / przerwanie przytrzymania */
    void StopHoldingInteraction();

    /** Czy aktualnie trwa proces kanałowania (Hold) */
    UFUNCTION(BlueprintPure, Category = "Custom|Interaction")
    bool IsInteracting() const { return bIsHolding; }

    /** Zwraca aktualny postęp przytrzymania w zakresie [0.0 - 1.0] */
    UFUNCTION(BlueprintPure, Category = "Custom|Interaction")
    float GetInteractionProgress() const { return CurrentHoldDuration > 0.0f ? FMath::Clamp(CurrentHoldTime / CurrentHoldDuration, 0.0f, 1.0f) : 0.0f; }

    // --- Delegaty dla UI i Gameplayu ---
    UPROPERTY(BlueprintAssignable, Category = "Custom|Interaction")
    FOnInteractionProgressChanged OnInteractionProgressChanged;

    UPROPERTY(BlueprintAssignable, Category = "Custom|Interaction")
    FOnInteractionCompleted OnInteractionCompleted;

    // Gettery konfiguracji
    float GetTraceDistance() const { return TraceDistance; }
    float GetTraceRadius() const { return InteractionTraceRadius; }
    ECollisionChannel GetInteractionChannel() const { return InteractionChannel; }

protected:
    /** Maksymalny dystans interakcji w cm liczony od postaci */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    float TraceDistance = 300.0f;

    /** Promień sfery testu kolizji (cm). Wartość > 0 = Sphere Trace */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "0.0", ClampMax = "50.0"))
    float InteractionTraceRadius = 12.0f;

    /** Kanał kolizji używany do wykrywania obiektów interaktywnych */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    TEnumAsByte<ECollisionChannel> InteractionChannel = ECC_Visibility;

    /** Minimalny odstęp czasowy między interakcjami (anty-spam/debounce w sekundach) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "0.05", ClampMax = "0.5"))
    float MinInteractionInterval = 0.15f;

    /** Domyślny czas wymagany do przytrzymania interakcji kanałowanej (sekundy) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "0.0"))
    float DefaultHoldDuration = 0.0f;

    // --- RPCs Sieciowe ---
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestInteract(AActor* TargetActor);

private:
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

    /** Timestamp ostatniej akcji na serwerze (ochrona przed spamem RPC) */
    double LastServerInteractionTime = -100.0;

    /** Timestamp ostatniej akcji u klienta (debounce) */
    double LastClientInteractionTime = -100.0;

    // Stan trzymania / kanałowania
    bool bIsHolding = false;
    float CurrentHoldTime = 0.0f;
    float CurrentHoldDuration = 0.0f;

    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> CurrentInteractingActor;
};

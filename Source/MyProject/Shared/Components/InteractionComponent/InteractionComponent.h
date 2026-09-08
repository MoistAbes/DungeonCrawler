#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "InteractionComponent.generated.h"

class ICarryAnchorProviderInterface;

/**
 * Formalna maszyna stanów niesienia i interakcji z obiektami fizycznymi.
 */
UENUM(BlueprintType)
enum class ECarryState : uint8
{
    /** Brak interakcji, postać ma wolne ręce */
    None            UMETA(DisplayName = "None"),

    /** Klient wysłał żądanie podniesienia i oczekuje na autorytatywną zgodę serwera */
    RequestingGrab  UMETA(DisplayName = "Requesting Grab"),

    /** Obiekt jest aktywnie niesiony w rękach (prowadzony kinematycznie) */
    Carrying        UMETA(DisplayName = "Carrying"),

    /** W trakcie zwalniania/upuszczania lub rzutu */
    Releasing       UMETA(DisplayName = "Releasing")
};

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

    /** Główna akcja: podnieś lub upuść obiekt pod celownikiem / rzuć zamachem myszką (Klawisz E) */
    void PrimaryInteract();

    /** Dedykowana akcja autorytatywnego rzutu na wprost (LPM) */
    void ThrowCurrentProp();

    /** Powiadamia komponent o podpięciu lub odpięciu niesionego propa (dla włączenia/wyłączenia On-Demand Tick) */
    void NotifyCarriedPropAttached(AActor* InProp);
    void NotifyCarriedPropDetached();

    /** Zwraca aktualny stan maszyny stanów niesienia */
    UFUNCTION(BlueprintPure, Category = "Custom|State")
    ECarryState GetCarryState() const { return CarryState; }

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

    /** Próg prędkości (cm/s), poniżej którego sztuczne mikroruchy ciężkich fizycznych propów są wygaszane */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "0.0"))
    float VelocityStopThreshold = 60.0f;

    /** Minimalny odstęp czasowy między kolejnymi akcjami interakcji (anty-spam/debounce w sekundach) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction", meta = (ClampMin = "0.05", ClampMax = "0.5"))
    float MinInteractionInterval = 0.15f;

    /** Aktualny stan interakcji */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|State")
    ECarryState CarryState = ECarryState::None;

    // --- RPCs Sieciowe (Zarządzane przez Serwer) ---

    /** Żądanie klienta do serwera o podniesienie wskazanego propa */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);

    /** Żądanie klienta do serwera o dedykowany rzut na wprost (LPM) – wektor w 100% liczy serwer */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestForwardThrow();

    /** Żądanie klienta do serwera o upuszczenie pod nogi (zero prędkości) LUB rzut pędem zamachu myszką */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestDropOrSwing(const FVector_NetQuantize& SwingVelocity);

    /** Żądanie interakcji logicznej (dźwignia, przełącznik) na serwerze */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestInteract(AActor* TargetActor);

    /** Powiadomienie klienta przez serwer o odrzuceniu próby podniesienia (brak LoS, za daleko, obiekt zajęty) */
    UFUNCTION(Client, Reliable)
    void Client_GrabDenied();

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

    /** Obiekty fizyczne aktualnie kolidujące/nakładające się na niesiony prop (zarządzane zdarzeniowo bez broadphase co tick) */
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<UPrimitiveComponent>> OverlappingPhysicsComponents;

    /** Timestamp ostatniej przetworzonej akcji interakcji na serwerze (ochrona przed spamem RPC) */
    double LastServerInteractionTime = -100.0;

    /** Timestamp ostatniej akcji interakcji u lokalnego klienta (debounce) */
    double LastClientInteractionTime = -100.0;

    // --- Rejestracja i obsługa zdarzeń kolizji (Event-Driven Overlaps) ---
    void BindPropOverlapEvents(UPrimitiveComponent* PropComp);
    void UnbindPropOverlapEvents();

    UFUNCTION()
    void OnPropBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnPropEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    // --- Metody pomocnicze ogólne ---
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    bool PerformTrace(FHitResult& OutHit) const;
    bool CanGrabServer(const AActor* TargetActor, const UPrimitiveComponent* ComponentToGrab) const;
    FVector CalculateServerThrowVelocity() const;
    void ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);
    void ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity);
    void ResetGrabState();
    void StopHeavyPhysicsObject(UPrimitiveComponent* Comp, float MaxPushableMass);

    // --- Metody pomocnicze kinematyki i fizyki niesionego obiektu ---
    FVector CalculateHoldAnchorRelativeOffset(float AimPitch, float BaseEyeHeightOffset) const;
    void UpdateHoldAnchorTransform(float DeltaTime);
    void UpdateCarriedPropTransform(float DeltaTime);
    void UpdateSwingVelocity(float DeltaTime);
    void HandleSweepCollision(const FHitResult& SweepHit, ICarryAnchorProviderInterface* CarryProvider, AActor* CarrierActor);
    void SuppressOverlappingHeavyPhysics(ICarryAnchorProviderInterface* CarryProvider);
    bool CheckGripBreakDistance(const FVector& TargetLocation);
};

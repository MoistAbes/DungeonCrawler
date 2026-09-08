#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "PhysicsCarryComponent.generated.h"

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
 * Konfiguracja parametrów przestrzennych i kinematycznych gniazda niesienia propa.
 * Zastępuje sztywne wartości (Magic Numbers) w matematyce pozycjonowania i interpolacji.
 */
USTRUCT(BlueprintType)
struct FCarrySocketConfig
{
    GENERATED_BODY()

    /** Dystans niesienia przedmiotu przed postacią w centymetrach */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "50.0", ClampMax = "200.0"))
    float HoldDistance = 110.0f;

    /** Obniżenie punktu trzymania w osi Z względem oczu postaci (cm) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "-50.0", ClampMax = "50.0"))
    float EyeHeightOffsetZ = -15.0f;

    /** Minimalny kąt pochylenia kamery w dół (stopnie) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "-80.0", ClampMax = "0.0"))
    float MinPitch = -50.0f;

    /** Maksymalny kąt pochylenia kamery w górę (stopnie) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0.0", ClampMax = "80.0"))
    float MaxPitch = 50.0f;

    /** Szybkość interpolacji kotwicy rąk za kamerą (VInterpTo speed) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "5.0", ClampMax = "50.0"))
    float AnchorInterpSpeed = 20.0f;

    /** Szybkość dociągania fizycznego propa do rąk (daje wrażenie bezwładności/masy) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "5.0", ClampMax = "50.0"))
    float PropInterpSpeed = 25.0f;

    /** Szybkość wygładzania prędkości kątowej przy zamachu myszką */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "5.0", ClampMax = "30.0"))
    float SwingInterpSpeed = 16.0f;
};

/**
 * Dedykowany komponent domenowy odpowiedzialny WYŁĄCZNIE za manipulację, niesienie i rzucanie obiektami fizycznymi.
 * Izoluje kinematyczny sweep, synchronizację sieciową chwytu, śledzenie pędu zamachu myszką i siłę rzutu na wprost.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UPhysicsCarryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPhysicsCarryComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Główna metoda chwytu wskazanego obiektu fizycznego (wywoływana przez postać po udanym trace) */
    void TryGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);

    /** Dedykowana akcja rzutu na wprost (Klawisz R / LPM) */
    void ThrowCurrentProp();

    /** Upuszczenie pod nogi (mała prędkość) LUB rzut zamachem myszką (Klawisz E w trakcie niesienia) */
    void DropOrSwing();

    /** Powiadamia komponent o podpięciu lub odpięciu niesionego propa (dla włączenia/wyłączenia On-Demand Tick) */
    void NotifyCarriedPropAttached(AActor* InProp);
    void NotifyCarriedPropDetached();

    /** Zwraca true, jeśli postać aktywnie niesie obiekt */
    UFUNCTION(BlueprintPure, Category = "Custom|Carry")
    bool IsCarrying() const { return CarryState == ECarryState::Carrying && IsValid(GrabbedActor); }

    /** Zwraca aktualny stan maszyny stanów niesienia */
    UFUNCTION(BlueprintPure, Category = "Custom|Carry")
    ECarryState GetCarryState() const { return CarryState; }

    /** Zwraca aktualnie trzymanego aktora */
    UFUNCTION(BlueprintPure, Category = "Custom|Carry")
    AActor* GetGrabbedActor() const { return GrabbedActor; }

protected:
    /** Parametry przestrzenne i interpolacji gniazda trzymania propa */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Config")
    FCarrySocketConfig CarrySocketConfig;

    /** Maksymalna masa obiektu, jaką postać może unieść (kg) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics")
    float MaxCarryMass = 35.0f;

    /** Siła pędu nadawanego obiektowi przy dedykowanym rzucie na wprost (Klawisz R) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics")
    float ThrowImpulseStrength = 1400.0f;

    /** Maksymalny dopuszczalny dystans rozciągnięcia rąk od zablokowanego propa (cm). Przekroczenie powoduje upuszczenie */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics", meta = (ClampMin = "30.0", ClampMax = "150.0"))
    float CarryBreakDistance = 70.0f;

    /** Mnożnik pędu nadawanego propowi przy rzucie zamachem myszką (klawisz E) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics", meta = (ClampMin = "0.5", ClampMax = "2.5"))
    float SwingVelocityMultiplier = 1.2f;

    /** Minimalna prędkość kątowa zamachu myszką (cm/s), by została uznana za rzut zamachem */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics", meta = (ClampMin = "100.0", ClampMax = "600.0"))
    float MinSwingSpeedToThrow = 250.0f;

    /** Maksymalna dopuszczalna prędkość rzutu zamachem (zabezpieczenie fizyki i sieci) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics", meta = (ClampMin = "500.0", ClampMax = "5000.0"))
    float MaxSwingThrowSpeed = 2200.0f;

    /** Próg prędkości (cm/s), poniżej którego sztuczne mikroruchy ciężkich fizycznych propów są wygaszane */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Physics", meta = (ClampMin = "0.0"))
    float VelocityStopThreshold = 60.0f;

    /** Minimalny odstęp czasowy między kolejnymi akcjami interakcji fizycznej (anty-spam w sekundach) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Carry|Network", meta = (ClampMin = "0.05", ClampMax = "0.5"))
    float MinInteractionInterval = 0.15f;

    /** Aktualny stan interakcji fizycznej */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Carry|State")
    ECarryState CarryState = ECarryState::None;

    // --- RPCs Sieciowe (Zarządzane przez Serwer) ---

    /** Żądanie klienta do serwera o podniesienie wskazanego propa */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);

    /** Żądanie klienta do serwera o dedykowany rzut na wprost (R / LPM) */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestForwardThrow();

    /** Żądanie klienta do serwera o upuszczenie pod nogi LUB rzut pędem zamachu myszką */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestDropOrSwing(const FVector_NetQuantize& SwingVelocity);

    /** Powiadomienie klienta przez serwer o odrzuceniu próby podniesienia */
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

    /** Obiekty fizyczne aktualnie kolidujące/nakładające się na niesiony prop */
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<UPrimitiveComponent>> OverlappingPhysicsComponents;

    /** Timestamp ostatniej przetworzonej akcji na serwerze (ochrona przed spamem RPC) */
    double LastServerInteractionTime = -100.0;

    /** Timestamp ostatniej akcji u lokalnego klienta (debounce) */
    double LastClientInteractionTime = -100.0;

    // --- Rejestracja i obsługa zdarzeń kolizji (Event-Driven Overlaps) ---
    void BindPropOverlapEvents(UPrimitiveComponent* PropComp);
    void UnbindPropOverlapEvents();

    UFUNCTION()
    void OnPropBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnPropEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    // --- Metody pomocnicze ogólne i kinetyczne ---
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    bool CanGrabServer(const AActor* TargetActor, const UPrimitiveComponent* ComponentToGrab) const;
    FVector CalculateServerThrowVelocity() const;
    void ExecuteGrab(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab);
    void ExecuteRelease(bool bIsThrow, const FVector& LaunchVelocity);
    void ResetGrabState();
    void StopHeavyPhysicsObject(UPrimitiveComponent* Comp, float MaxPushableMass);

    FVector CalculateHoldAnchorRelativeOffset(float AimPitch, float BaseEyeHeightOffset) const;
    void UpdateHoldAnchorTransform(float DeltaTime);
    void UpdateCarriedPropTransform(float DeltaTime);
    void UpdateSwingVelocity(float DeltaTime);
    void HandleSweepCollision(const FHitResult& SweepHit, ICarryAnchorProviderInterface* CarryProvider, AActor* CarrierActor);
    void SuppressOverlappingHeavyPhysics(ICarryAnchorProviderInterface* CarryProvider);
    bool CheckGripBreakDistance(const FVector& TargetLocation);
};

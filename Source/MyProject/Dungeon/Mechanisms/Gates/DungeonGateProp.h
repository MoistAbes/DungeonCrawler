#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "MyProject/Shared/Interfaces/MechanismReceiverInterface.h"
#include "MyProject/Shared/Interfaces/SurfaceGridTargetInterface.h"
#include "DungeonGateProp.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UDamageableComponent;

/**
 * Stan logiczny i kinematyczny bramy lochu
 */
UENUM(BlueprintType)
enum class EGateState : uint8
{
	Closed UMETA(DisplayName = "Closed"),
	Opening UMETA(DisplayName = "Opening"),
	Open UMETA(DisplayName = "Open"),
	Closing UMETA(DisplayName = "Closing")
};

/**
 * Zachowanie bramy przy napotkaniu przeszkody podczas opadania (Anti-Crush)
 */
UENUM(BlueprintType)
enum class EGateBlockBehavior : uint8
{
	WaitAndCrush UMETA(DisplayName = "Wait and Crush"),
	Rebound UMETA(DisplayName = "Rebound (Reverse to Open)")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGateStateChangedSignature, EGateState, NewState, AActor*, TriggeringActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGateSlamDownSignature);

/**
 * Autorytatywna brama lochu / krata podnoszona (ADungeonGateProp).
 * 
 * Zapewnia:
 * - Implementację IMechanismReceiverInterface (reakcja na sygnały z płyt naciskowych, dźwigni, switchy).
 * - Tożsamość materiałową (Metal/Stone/Wood) oraz obsługę komórek powierzchniowych (ISurfaceGridTargetInterface).
 * - Kinematyczny ruch w osi pionowej (lub dowolnym wektorze OpenOffset) z testem kolizji bSweep = true.
 * - Anti-Crush System: zadawanie obrażeń miażdżących obiektom blokującym opadanie oraz opcjonalny Rebound.
 * - Zero-Bandwidth Networking: replikacja stanu GateState, Tick on-demand aktywny wyłącznie podczas ruchu.
 */
UCLASS()
class MYPROJECT_API ADungeonGateProp : public AActor, public IMechanismReceiverInterface, public IMaterialProviderInterface, public ISurfaceGridTargetInterface
{
	GENERATED_BODY()

public:
	ADungeonGateProp();

	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- IMaterialProviderInterface ---
	virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

	// --- ISurfaceGridTargetInterface ---
	virtual bool CanReceiveSurfaceCells_Implementation() const override;
	virtual bool IsDynamicSurface_Implementation() const override { return true; }

	// --- IMechanismReceiverInterface ---
	virtual void SetMechanismState_Implementation(bool bActive, AActor* TriggeringActor) override;

	// --- Sterowanie Bramą ---

	/** Zwraca bieżący stan fazy ruchu lub spoczynku bramy */
	UFUNCTION(BlueprintPure, Category = "Custom|Gate")
	EGateState GetGateState() const { return GateState; }

	/** Czy brama jest w pełni otwarta */
	UFUNCTION(BlueprintPure, Category = "Custom|Gate")
	bool IsOpen() const { return GateState == EGateState::Open; }

	/** Czy brama jest w pełni zamknięta */
	UFUNCTION(BlueprintPure, Category = "Custom|Gate")
	bool IsClosed() const { return GateState == EGateState::Closed; }

	/** Czy brama aktualnie się przemieszcza */
	UFUNCTION(BlueprintPure, Category = "Custom|Gate")
	bool IsMoving() const { return GateState == EGateState::Opening || GateState == EGateState::Closing; }

	/** Otwiera bramę (autorytatywne) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Gate")
	void OpenGate(AActor* TriggeringActor = nullptr);

	/** Zamyka bramę (autorytatywne) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Gate")
	void CloseGate(AActor* TriggeringActor = nullptr);

	/** Przełącza stan bramy (jeśli otwarta/otwierająca się -> zamyka; w przeciwnym razie otwiera) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Gate")
	void ToggleGate(AActor* TriggeringActor = nullptr);

	// --- Gettery Komponentów ---

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetGateMeshComponent() const { return GateMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetFrameMeshComponent() const { return FrameMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

	// --- Delegaty ---

	/** Wywoływane przy każdej zmianie stanu logicznego bramy */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnGateStateChangedSignature OnGateStateChanged;

	/** Wywoływane w momencie uderzenia opadającej kraty o posadzkę (Slam Down) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnGateSlamDownSignature OnGateSlamDown;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void StartOpening(AActor* TriggeringActor);
	void StartClosing(AActor* TriggeringActor);
	void HandleReachedOpen();
	void HandleReachedClosed();

	/** Obsługa zablokowania opadającej kraty przez przeszkodę (gracz, skrzynia, prop) */
	void HandleBlockedByObstacle(const FHitResult& HitResult, float DeltaTime);

	/** Zadaje obrażenia miażdżące obiektowi blokującemu bramę */
	void ApplyCrushDamage(AActor* ObstacleActor);

	UFUNCTION()
	void OnRep_GateState();

	UFUNCTION()
	void HandleOnDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleAutoCloseTimer();

	UFUNCTION()
	void HandleReboundTimer();

	/** Blueprintowy hook do kosmetycznych efektów przy zmianie stanu (np. dźwięk łańcuchów, zgrzyt) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Gate", meta = (DisplayName = "OnGateStateChangedCosmetic"))
	void ReceiveGateStateChangedCosmetic(EGateState NewState);

	/** Blueprintowy hook do efektu uderzenia o posadzkę (np. tuman kurzu, głośny brzęk żelaza) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Gate", meta = (DisplayName = "OnGateSlamDownCosmetic"))
	void ReceiveGateSlamDownCosmetic();

	/** Blueprintowy hook do efektu uderzenia w przeszkodę (iskry, zgrzyt o pancerz) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Gate", meta = (DisplayName = "OnGateBlockedCosmetic"))
	void ReceiveGateBlockedCosmetic(const FHitResult& HitResult);

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Nieruchomy punkt bazowy na poziomie posadzki */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<USceneComponent> SceneRootComponent;

	/** Opcjonalna nieruchoma futryna / prowadnice boczne bramy */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> FrameMeshComponent;

	/** Ruchoma krata lub wrota unoszące się w górę */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> GateMeshComponent;

	/** Komponent wytrzymałości fizycznej (dla zniszczalnych drewnianych wrót lub żelaznej kraty) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDamageableComponent> DamageableComponent;

	// -------------------------------------------------------------------------
	// Konfiguracja Ruchu i Kinetyki
	// -------------------------------------------------------------------------

	/** Tożsamość materiałowa bramy (Metal, Stone, Wood) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Metal;

	/** Czy brama może ulec fizycznemu zniszczeniu przez ładunki wybuchowe lub taran */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Destruction")
	bool bIsDestructible = false;

	/** Czy brama na starcie poziomu ma być otwarta */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate")
	bool bStartOpen = false;

	/** Lokalny wektor przesunięcia kraty w stanie pełnego otwarcia (np. Z = +280 cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate")
	FVector OpenOffset = FVector(0.0f, 0.0f, 280.0f);

	/** Prędkość unoszenia się kraty (w cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate", meta = (ClampMin = "10.0"))
	float OpenSpeed = 150.0f;

	/** Prędkość grawitacyjnego opadania kraty (w cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate", meta = (ClampMin = "10.0"))
	float CloseSpeed = 350.0f;

	/** Czy brama po otwarciu powinna samoczynnie zamknąć się po upływie AutoCloseDelay */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate|AutoClose")
	bool bAutoClose = false;

	/** Czas oczekiwania w pełnym otwarciu przed samoczynnym opuszczeniem kraty (w sekundach) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate|AutoClose", meta = (EditCondition = "bAutoClose", ClampMin = "0.5"))
	float AutoCloseDelay = 5.0f;

	// -------------------------------------------------------------------------
	// Anti-Crush System
	// -------------------------------------------------------------------------

	/** Sposób zachowania opadającej bramy po zablokowaniu przez przeszkodę */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate|AntiCrush")
	EGateBlockBehavior BlockBehavior = EGateBlockBehavior::WaitAndCrush;

	/** Obrażenia miażdżące zadawane obiektom z UDamageableComponent blokującym opadanie */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate|AntiCrush", meta = (ClampMin = "0.0"))
	float CrushDamage = 35.0f;

	/** Minimalny odstęp czasu między kolejnymi uderzeniami miażdżącymi na zablokowanej bramie (s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate|AntiCrush", meta = (ClampMin = "0.1"))
	float CrushDamageInterval = 0.5f;

	/** Czas oczekiwania przed ponowną próbą opuszczenia bramy po odbiciu (dla trybu Rebound) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Gate|AntiCrush", meta = (EditCondition = "BlockBehavior == EGateBlockBehavior::Rebound", ClampMin = "0.2"))
	float ReboundDelay = 1.5f;

private:
	/** Replikowany bieżący stan bramy */
	UPROPERTY(ReplicatedUsing = OnRep_GateState, VisibleInstanceOnly, Category = "Custom|Gate")
	EGateState GateState = EGateState::Closed;

	/** Początkowa lokalna pozycja kraty w spoczynku (Closed) */
	FVector DefaultGateRelativeLocation = FVector::ZeroVector;

	/** Znacznik czasu ostatniego zadania obrażeń miażdżących */
	float LastCrushDamageTime = -100.0f;

	/** Ostatni aktor wyzwalający ruch bramy */
	TWeakObjectPtr<AActor> LastTriggeringActor = nullptr;

	/** Timer samoczynnego zamykania */
	FTimerHandle AutoCloseTimerHandle;

	/** Timer ponownej próby opuszczenia po odbiciu (Rebound) */
	FTimerHandle ReboundTimerHandle;
};

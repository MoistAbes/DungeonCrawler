#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "MyProject/Shared/Interfaces/MechanismReceiverInterface.h"
#include "MechanismTrapBase.generated.h"

class UStaticMeshComponent;
class UDamageableComponent;
class UStatusEffectComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrapTriggeredSignature, AActor*, InstigatorActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrapActiveStateChangedSignature, bool, bIsActive);

/**
 * Bazowa klasa domenowa dla wszystkich mechanicznych pułapek środowiskowych w lochu
 * (tłoki ścienne/podłogowe, miotacze pocisków, wirujące ostrza, pułapki płomieniowe).
 * 
 * Implementuje:
 * - IMechanismReceiverInterface (reakcja na sygnały z przełączników ASwitchPropBase i płyt APressurePlateProp).
 * - IMaterialProviderInterface (tożsamość materiałowa).
 * - Świętą Trójcę (StaticMesh bazy, UDamageableComponent, UStatusEffectComponent).
 * - Tryb pętli czasowej (bIsContinuousLoop) oraz tryb wyzwalany sygnałem ze świata.
 * - Server-Authoritative First – cała logika aktywacji i fizyki jest autorytatywna.
 */
UCLASS(Abstract)
class MYPROJECT_API AMechanismTrapBase : public AActor, public IMechanismReceiverInterface, public IMaterialProviderInterface
{
	GENERATED_BODY()

public:
	AMechanismTrapBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- IMaterialProviderInterface ---
	virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

	// --- IMechanismReceiverInterface ---
	virtual void SetMechanismState_Implementation(bool bActive, AActor* TriggeringActor) override;

	// --- Sterowanie Pułapką ---

	/** Czy pułapka jest aktualnie uzbrojona / włączona */
	UFUNCTION(BlueprintPure, Category = "Custom|Trap")
	bool IsTrapActive() const { return bIsTrapActive; }

	/** Zmienia stan uzbrojenia pułapki (włącza/wyłącza pętlę lub gotowość) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Trap")
	virtual void SetTrapActive(bool bNewActive, AActor* TriggeringActor);

	/** 
	 * Jednorazowo wyzwala akcję pułapki (np. uderzenie tłoka, wystrzelenie pocisku).
	 * Wykonywane wyłącznie na serwerze.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|Trap")
	virtual void TriggerTrap(AActor* TriggeringActor);

	// --- Gettery Komponentów ---

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetBaseMeshComponent() const { return BaseMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStatusEffectComponent* GetStatusEffectComponent() const { return StatusEffectComponent; }

	// --- Delegaty ---

	/** Wywoływane przy każdym fizycznym wyzwoleniu pułapki */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnTrapTriggeredSignature OnTrapTriggered;

	/** Wywoływane przy zmianie stanu uzbrojenia pułapki */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnTrapActiveStateChangedSignature OnTrapActiveChanged;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Czysto domenowa akcja pułapki implementowana przez klasy potomne
	 * (np. uderzenie tłoka w APistonTrap, wystrzelenie w AProjectileLauncherTrap).
	 */
	virtual void ExecuteTrapAction(AActor* TriggeringActor) PURE_VIRTUAL(AMechanismTrapBase::ExecuteTrapAction, );

	/** Hook dla Blueprintów wywoływany przy wyzwoleniu pułapki (dźwięki, cząsteczki) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Trap", meta = (DisplayName = "OnTrapTriggeredCosmetic"))
	void ReceiveTrapTriggeredCosmetic();

	/** Hook dla Blueprintów wywoływany przy zmianie uzbrojenia pułapki */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Trap", meta = (DisplayName = "OnTrapActiveChangedCosmetic"))
	void ReceiveTrapActiveChangedCosmetic(bool bNewActive);

	UFUNCTION()
	virtual void OnRep_IsTrapActive();

	virtual void StartLoopTimer();
	virtual void StopLoopTimer();

	UFUNCTION()
	virtual void HandleLoopTimerTick();

	UFUNCTION()
	virtual void HandleOnDestroyed(AActor* DestroyedActor);

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Nieruchoma obudowa / rama pułapki wmurowana w ścianę lub posadzkę */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> BaseMeshComponent;

	/** Komponent wytrzymałości fizycznej (pozwala na rozbicie pułapki) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDamageableComponent> DamageableComponent;

	/** Komponent obsługujący stany żywiołowe */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStatusEffectComponent> StatusEffectComponent;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Tożsamość materiałowa bazy pułapki (Stone, Metal) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Stone;

	/** Czy pułapka może ulec fizycznemu zniszczeniu */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Destruction")
	bool bIsDestructible = false;

	/** 
	 * Czy pułapka działa samoczynnie w ciągłej pętli czasowej.
	 * Jeśli false – czeka w uśpieniu na sygnał ze switcha/płyty naciskowej.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Timing")
	bool bIsContinuousLoop = false;

	/** Czy pułapka powinna automatycznie uzbroić się i wystartować pętlę przy starcie poziomu */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Timing", meta = (EditCondition = "bIsContinuousLoop"))
	bool bAutoActivateOnBeginPlay = true;

	/** Odstęp czasu (w sekundach) między kolejnymi wyzwoleniami w pętli */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Timing", meta = (EditCondition = "bIsContinuousLoop", ClampMin = "0.2"))
	float LoopInterval = 3.0f;

	/** Początkowe opóźnienie (w sekundach) przed pierwszym wystrzałem w pętli */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Timing", meta = (EditCondition = "bIsContinuousLoop", ClampMin = "0.0"))
	float InitialDelay = 0.0f;

	// -------------------------------------------------------------------------
	// Replikacja Stanu
	// -------------------------------------------------------------------------

	/** Replikowany stan uzbrojenia / aktywności pułapki */
	UPROPERTY(ReplicatedUsing = OnRep_IsTrapActive, VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Trap")
	bool bIsTrapActive = false;

	/** Uchwyt timera pętli ciągłej */
	FTimerHandle LoopTimerHandle;
};

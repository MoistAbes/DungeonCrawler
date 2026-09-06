#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Mechanisms/MechanismTrapBase/MechanismTrapBase.h"
#include "PistonTrap.generated.h"

class UBoxComponent;

/**
 * Fazy cyklu pracy tłoka kamiennego / taranu
 */
UENUM(BlueprintType)
enum class EPistonState : uint8
{
	IdleAtHome UMETA(DisplayName = "Idle At Home"),
	Extending UMETA(DisplayName = "Extending"),
	HoldingAtExtended UMETA(DisplayName = "Holding At Extended"),
	Retracting UMETA(DisplayName = "Retracting")
};

/**
 * Tłok kamienny / Taran ścienny / Katapulta podłogowa (APistonTrap).
 * 
 * Zapewnia:
 * - Gwałtowny impet kinetyczny w wybranym kierunku (PushDirection: ściana, podłoga, sufit).
 * - Odrzut postaci (CMC LaunchCharacter) oraz impulsy fizyczne dla głazów i barykad.
 * - Zadawanie obrażeń kinetycznych (UDamageableComponent).
 * - Działanie w pętli ciągłej LUB sterowanie sygnałem przez IMechanismReceiverInterface.
 * - Optymalizację Zero-Tick: Tick aktywny wyłącznie podczas fizycznego ruchu głowicy.
 */
UCLASS()
class MYPROJECT_API APistonTrap : public AMechanismTrapBase
{
	GENERATED_BODY()

public:
	APistonTrap();

	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Zwraca bieżący stan fazy pracy tłoka */
	UFUNCTION(BlueprintPure, Category = "Custom|Piston")
	EPistonState GetPistonState() const { return PistonState; }

	/** Zwraca ruchomą głowicę tłoka */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetPistonHeadMesh() const { return PistonHeadMesh; }

	/** Zwraca strefę uderzenia kinetycznego */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UBoxComponent* GetDamageBox() const { return DamageBox; }

protected:
	virtual void BeginPlay() override;
	virtual void ExecuteTrapAction(AActor* TriggeringActor) override;

	UFUNCTION()
	void HandleDamageBoxBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Rozpoczyna powrót tłoka do pozycji wyjściowej */
	void BeginRetracting();

	/** Aplikuje uderzenie kinetyczne na trafiony obiekt */
	void ApplyKineticHit(AActor* HitActor, UPrimitiveComponent* HitComponent);

	UFUNCTION()
	void OnRep_PistonState();

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Ruchoma głowica taranu/tłoka uderzająca w przestrzeń */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> PistonHeadMesh;

	/** Strefa kolizyjna uderzenia na czole głowicy tłoka */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UBoxComponent> DamageBox;

	// -------------------------------------------------------------------------
	// Konfiguracja Ruchu i Kinetyki
	// -------------------------------------------------------------------------

	/** Lokalny wektor kierunku wysunięcia (np. (1,0,0) w przód ściany, (0,0,1) pionowo w górę) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Piston")
	FVector PushDirection = FVector(1.0f, 0.0f, 0.0f);

	/** Maksymalny dystans wysunięcia tłoka (w cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Piston", meta = (ClampMin = "50.0"))
	float StrokeDistance = 300.0f;

	/** Prędkość uderzenia / wysunięcia tłoka (w cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Piston", meta = (ClampMin = "100.0"))
	float ExtendSpeed = 1200.0f;

	/** Prędkość powolnego cofania tłoka (w cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Piston", meta = (ClampMin = "50.0"))
	float RetractSpeed = 150.0f;

	/** Czas odczekania (w sekundach) w pełnym wysunięciu przed rozpoczęciem powrotu */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Piston", meta = (ClampMin = "0.05"))
	float RetractDelay = 0.5f;

	/** Prędkość odrzutu postaci uderzonych przez taran (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "0.0"))
	float KnockbackSpeed = 1800.0f;

	/** Obrażenia zadawane obiektom z UDamageableComponent przy bezpośrednim uderzeniu */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "0.0"))
	float BaseImpactDamage = 40.0f;

	/** Mnożnik impulsu przekazywanego obiektom symulującym fizykę Chaos (np. głazy, skrzynie) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "10.0"))
	float PhysicsImpulseMultiplier = 1500.0f;

private:
	/** Replikowany bieżący stan fazy pracy tłoka */
	UPROPERTY(ReplicatedUsing = OnRep_PistonState, VisibleInstanceOnly, Category = "Custom|Piston")
	EPistonState PistonState = EPistonState::IdleAtHome;

	/** Aktualne lokalne wysunięcie tłoka (0 do StrokeDistance) */
	float CurrentStrokeDistance = 0.0f;

	/** Początkowa lokalna pozycja głowicy */
	FVector DefaultHeadRelativeLocation = FVector::ZeroVector;

	/** Znormalizowany kierunek uderzenia w przestrzeni lokalnej */
	FVector NormalizedPushDirection = FVector(1.0f, 0.0f, 0.0f);

	/** Zbiór aktorów trafionych podczas bieżącego uderzenia (zapobiega wielokrotnemu trafieniu w jednym suwie) */
	TSet<TWeakObjectPtr<AActor>> HitActorsThisStroke;

	/** Uchwyt timera pauzy w pełnym wysunięciu */
	FTimerHandle RetractTimerHandle;
};

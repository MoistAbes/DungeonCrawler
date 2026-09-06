#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Props/SwitchPropBase/SwitchPropBase.h"
#include "PressurePlateProp.generated.h"

class UBoxComponent;

/**
 * Fizyczna płyta naciskowa reagująca na rzeczywistą masę znajdujących się na niej ciał.
 * 
 * Działanie:
 * - Serwer autorytatywnie sumuje masę nakładających się obiektów (gracze, głazy, skrzynie).
 * - Gdy sumaryczna masa osiągnie lub przekroczy RequiredMass (np. 50 kg), płyta aktywuje się (ON).
 * - Gdy masa spadnie poniżej progu, płyta dezaktywuje się (OFF) – o ile bAllowSwitchBack jest włączone.
 * - Wyposażona w ruchomy kafel (PlateMesh) z płynnym zapadaniem się (Tick On-Demand)
 *   oraz pełną replikację stanu Co-op.
 */
UCLASS()
class MYPROJECT_API APressurePlateProp : public ASwitchPropBase
{
	GENERATED_BODY()

public:
	APressurePlateProp();

	virtual void Tick(float DeltaTime) override;

	/** Zwraca aktualną łączną masę spoczywającą na płycie naciskowej (kg) */
	UFUNCTION(BlueprintPure, Category = "Custom|PressurePlate")
	float GetCurrentTotalMass() const { return CurrentTotalMass; }

	/** Zwraca minimalną wymaganą masę do wciśnięcia płyty (kg) */
	UFUNCTION(BlueprintPure, Category = "Custom|PressurePlate")
	float GetRequiredMass() const { return RequiredMass; }

	/** Zwraca komponent strefy detekcji masy */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UBoxComponent* GetTriggerBox() const { return TriggerBox; }

	/** Zwraca ruchomą siatkę kafla płyty */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetPlateMesh() const { return PlateMesh; }

protected:
	virtual void BeginPlay() override;
	virtual void HandleStateChanged(bool bNewState, AActor* TriggeringActor) override;

	/** Aktualizuje sumę mas obiektów w strefie i ewentualnie zmienia stan płyty */
	void RecalculateMassAndEvaluate();

	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleTriggerEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Ruchomy kafel kamienny zapadający się pod ciężarem */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> PlateMesh;

	/** Objętość detekcji ciał fizycznych i postaci */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	// -------------------------------------------------------------------------
	// Konfiguracja Fizyczna Płyty
	// -------------------------------------------------------------------------

	/** Minimalna masa (w kg) wymagana do dociśnięcia płyty i wyzwolenia sygnału */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|PressurePlate", meta = (ClampMin = "1.0"))
	float RequiredMass = 50.0f;

	/** Domyślna masa przypisywana kinematycznym postaciom graczy/wrogów (w kg) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|PressurePlate", meta = (ClampMin = "1.0"))
	float DefaultCharacterMass = 80.0f;

	/** Głębokość zapadania się kafla po aktywacji (cm w osi Z) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|PressurePlate", meta = (ClampMin = "1.0"))
	float DepressionDepth = 6.0f;

	/** Prędkość płynnego zapadania / unoszenia się kafla (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|PressurePlate", meta = (ClampMin = "5.0"))
	float PlateMoveSpeed = 30.0f;

private:
	/** Aktualna zliczona masa spoczywająca na płycie */
	UPROPERTY(VisibleInstanceOnly, Category = "Custom|PressurePlate")
	float CurrentTotalMass = 0.0f;

	/** Początkowa lokalna pozycja kafla Z */
	float DefaultPlateZ = 0.0f;

	/** Docelowa lokalna pozycja kafla Z */
	float TargetPlateZ = 0.0f;

	/** Rejestr aktualnie nakładających się komponentów i ich mas */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, float> OverlappingComponents;

	/** Ostatni aktor wyzwalający zmianę stanu */
	TWeakObjectPtr<AActor> LastTriggeringActor = nullptr;
};

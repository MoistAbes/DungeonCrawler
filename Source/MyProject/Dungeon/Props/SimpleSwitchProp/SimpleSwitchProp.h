#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "SimpleSwitchProp.generated.h"

class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSwitchToggledSignature, bool, bNewState, AActor*, Interactor);

/**
 * Domenowa encja przełącznika/dźwigni w lochu.
 * Implementuje kontrakt logiczny (IInteractableInterface) oraz tożsamość materiałową.
 */
UCLASS()
class MYPROJECT_API ASimpleSwitchProp : public AActor, public IInteractableInterface, public IMaterialProviderInterface
{
	GENERATED_BODY()

public:
	ASimpleSwitchProp();

	// --- IMaterialProviderInterface ---
	virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

	// --- IInteractableInterface Contract ---
	virtual void Interact(AActor* Interactor) override;
	virtual bool CanInteract(const AActor* Interactor) const override;
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;

	// --- Gettery / Settery ---

	/** Zwraca bieżący stan logiczny przełącznika (true = włączony / aktywny, false = wyłączony) */
	UFUNCTION(BlueprintPure, Category = "Custom|Switch")
	bool IsActive() const { return bIsActive; }

	/** Sprawdza, czy gracz może wejść w interakcję z tym przełącznikiem */
	UFUNCTION(BlueprintPure, Category = "Custom|Switch")
	bool CanBeUsed() const { return bCanBeUsed; }

	/** Zmienia możliwość użycia przełącznika (np. odblokowanie po znalezieniu klucza lub włączeniu zasilania) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Switch")
	void SetCanBeUsed(bool bInCanBeUsed) { bCanBeUsed = bInCanBeUsed; }

	// --- Eventy ---

	/** Wywoływane za każdym razem, gdy stan przełącznika ulegnie zmianie (zwraca nowy stan oraz aktora przełączającego) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnSwitchToggledSignature OnSwitchToggled;

protected:
	/** Siatka statyczna reprezentująca przełącznik lub dźwignię */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Tożsamość materiałowa przełącznika (np. Metal) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Metal;

	/** Stan logiczny przełącznika (np. ON/OFF, otwarty/zamknięty) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Custom|Switch")
	bool bIsActive = false;

	/** Flaga blokady biznesowej (np. brak zasilania, zablokowany zamek). Gdy false, gracz nie może przełączyć dźwigni */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Switch")
	bool bCanBeUsed = true;
};

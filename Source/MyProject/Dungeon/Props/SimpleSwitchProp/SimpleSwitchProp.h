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
	UFUNCTION(BlueprintPure, Category = "Switch|State")
	bool IsActive() const { return bIsActive; }

	UFUNCTION(BlueprintPure, Category = "Switch|State")
	bool CanBeUsed() const { return bCanBeUsed; }

	UFUNCTION(BlueprintCallable, Category = "Switch|State")
	void SetCanBeUsed(bool bInCanBeUsed) { bCanBeUsed = bInCanBeUsed; }

	// --- Eventy ---
	UPROPERTY(BlueprintAssignable, Category = "Switch|Events")
	FOnSwitchToggledSignature OnSwitchToggled;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Switch|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Metal;

	/** Stan logiczny przełącznika (np. ON/OFF) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch|State")
	bool bIsActive = false;

	/** Flaga blokady biznesowej (np. brak zasilania) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|State")
	bool bCanBeUsed = true;
};

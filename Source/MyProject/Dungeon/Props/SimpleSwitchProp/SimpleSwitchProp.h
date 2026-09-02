#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "SimpleSwitchProp.generated.h"

class UStaticMeshComponent;

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

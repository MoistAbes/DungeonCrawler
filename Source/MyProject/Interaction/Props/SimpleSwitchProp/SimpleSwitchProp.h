#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Interaction/Interfaces/InteractableInterface/IInteractableInterface.h"
#include "SimpleSwitchProp.generated.h"

class UStaticMeshComponent;

/**
 * Domenowa encja przełącznika/dźwigni.
 * Implementuje wyłącznie kontrakt logiczny (IInteractableInterface).
 */
UCLASS()
class MYPROJECT_API ASimpleSwitchProp : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	ASimpleSwitchProp();

	// --- IInteractableInterface Contract ---
	virtual void Interact(AActor* Interactor) override;
	virtual bool CanInteract(const AActor* Interactor) const override;
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Stan logiczny przełącznika (np. ON/OFF) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch|State")
	bool bIsActive = false;

	/** Flaga blokady biznesowej (np. brak zasilania) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|State")
	bool bCanBeUsed = true;
};
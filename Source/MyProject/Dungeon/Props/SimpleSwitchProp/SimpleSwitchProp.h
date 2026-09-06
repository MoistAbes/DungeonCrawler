#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Props/SwitchPropBase/SwitchPropBase.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"
#include "SimpleSwitchProp.generated.h"

/**
 * Domenowa encja przełącznika/dźwigni ściennej w lochu aktywowanej ręcznie klawiszem interakcji.
 * Dziedziczy z ASwitchPropBase (pełna sieć Co-op, bAllowSwitchBack, TargetMechanisms)
 * oraz implementuje IInteractableInterface.
 */
UCLASS()
class MYPROJECT_API ASimpleSwitchProp : public ASwitchPropBase, public IInteractableInterface
{
	GENERATED_BODY()

public:
	ASimpleSwitchProp();

	// --- IInteractableInterface Contract ---
	virtual void Interact(AActor* Interactor) override;
	virtual bool CanInteract(const AActor* Interactor) const override;
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;
};

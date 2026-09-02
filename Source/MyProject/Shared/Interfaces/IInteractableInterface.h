#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IInteractableInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Odpowiednik: public interface InteractableService
 * Służy wyłącznie do wywołania logiki biznesowej klawiszem interakcji (np. dźwignia, przełącznik, NPC, pochodnia).
 */
class MYPROJECT_API IInteractableInterface
{
	GENERATED_BODY()

public:
	/** Główna akcja biznesowa (np. naciśnięcie klawisza E lub akcja AI) */
	virtual void Interact(AActor* Interactor) = 0;

	/** Warunek dostępności akcji */
	virtual bool CanInteract(const AActor* Interactor) const = 0;

	/** Opcjonalna podpowiedź tekstowa do UI/HUD */
	virtual FText GetInteractionPrompt(const AActor* Interactor) const { return FText::GetEmpty(); }
};

#include "SimpleSwitchProp.h"

ASimpleSwitchProp::ASimpleSwitchProp()
{
	// Domyślnie dźwignia lub przełącznik ścienny jest metalowy
	MaterialType = EPhysicalMaterialType::Metal;
}

void ASimpleSwitchProp::Interact(AActor* Interactor)
{
	if (!CanInteract(Interactor))
	{
		return;
	}

	SetActiveState(!bIsActive, Interactor);
}

bool ASimpleSwitchProp::CanInteract(const AActor* Interactor) const
{
	if (!bCanBeUsed)
	{
		return false;
	}

	// Jeśli przełącznik jest jednokierunkowy i został już przestawiony, blokujemy ponowną interakcję
	if (!bAllowSwitchBack && bHasBeenTriggered)
	{
		return false;
	}

	return true;
}

FText ASimpleSwitchProp::GetInteractionPrompt(const AActor* Interactor) const
{
	if (!bCanBeUsed)
	{
		return NSLOCTEXT("SwitchPrompt", "Inactive", "Locked");
	}

	if (!bAllowSwitchBack && bHasBeenTriggered)
	{
		return NSLOCTEXT("SwitchPrompt", "Activated", "Already Used");
	}

	return bIsActive 
		? NSLOCTEXT("SwitchPrompt", "TurnOff", "Turn OFF") 
		: NSLOCTEXT("SwitchPrompt", "TurnOn", "Turn ON");
}

#include "SimpleSwitchProp.h"
#include "Components/StaticMeshComponent.h"

ASimpleSwitchProp::ASimpleSwitchProp()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	MeshComponent->SetGenerateOverlapEvents(false);
}

void ASimpleSwitchProp::Interact(AActor* Interactor)
{
	if (!CanInteract(Interactor))
	{
		return;
	}

	bIsActive = !bIsActive;

	UE_LOG(LogTemp, Warning, TEXT("[SwitchEntity] Toggled state: %s by Interactor: %s"), 
		bIsActive ? TEXT("ON") : TEXT("OFF"), 
		Interactor ? *Interactor->GetName() : TEXT("Unknown"));
}

bool ASimpleSwitchProp::CanInteract(const AActor* Interactor) const
{
	return bCanBeUsed;
}

FText ASimpleSwitchProp::GetInteractionPrompt(const AActor* Interactor) const
{
	if (!bCanBeUsed)
	{
		return NSLOCTEXT("SwitchPrompt", "Inactive", "Locked");
	}

	return bIsActive 
		? NSLOCTEXT("SwitchPrompt", "TurnOff", "Turn OFF") 
		: NSLOCTEXT("SwitchPrompt", "TurnOn", "Turn ON");
}

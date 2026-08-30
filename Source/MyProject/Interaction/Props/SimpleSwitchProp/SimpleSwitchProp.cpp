#include "SimpleSwitchProp.h"
#include "Components/StaticMeshComponent.h"

ASimpleSwitchProp::ASimpleSwitchProp()
{
	// Optymalizacja: obiekt sterowany wyłącznie zdarzeniami (zero CPU Tick)
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	// Statyczna blokada kolizyjna (brak symulacji fizyki)
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	MeshComponent->SetGenerateOverlapEvents(false);
}

void ASimpleSwitchProp::Interact(AActor* Interactor)
{
	if (!CanInteract(Interactor))
	{
		return;
	}

	// Przełączenie stanu biznesowego (Toggle)
	bIsActive = !bIsActive;

	const FString InteractorName = Interactor ? Interactor->GetName() : TEXT("Unknown");
	UE_LOG(LogTemp, Warning, TEXT("[SwitchService] %s toggled by %s | New State: %s"), 
		*GetName(), *InteractorName, bIsActive ? TEXT("ACTIVE (ON)") : TEXT("INACTIVE (OFF)"));
}

bool ASimpleSwitchProp::CanInteract(const AActor* Interactor) const
{
	return bCanBeUsed;
}

FText ASimpleSwitchProp::GetInteractionPrompt(const AActor* Interactor) const
{
	if (!bCanBeUsed)
	{
		return FText::FromString(TEXT("Zablokowane"));
	}

	return bIsActive 
		? FText::FromString(TEXT("E - Wyłącz")) 
		: FText::FromString(TEXT("E - Włącz"));
}
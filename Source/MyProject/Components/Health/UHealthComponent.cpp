#include "UHealthComponent.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	MaxHealth = 100.0f;
	CurrentHealth = 75.0f;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);
}
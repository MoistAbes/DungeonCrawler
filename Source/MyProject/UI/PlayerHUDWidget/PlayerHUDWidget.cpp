#include "PlayerHUDWidget.h"
#include "../StatBarWidget.h"
#include "../../Components/Health/UHealthComponent.h"

void UPlayerHUDWidget::BindHealthComponent(UHealthComponent* InHealthComponent)
{
	if (!InHealthComponent) return;

	BoundHealthComponent = InHealthComponent;

	// Subskrypcja na strumień zdarzeń z serwisu zdrowia
	InHealthComponent->OnHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);

	// Synchronizacja stanu początkowego (Initial State Hydration)
	HandleHealthChanged(InHealthComponent->GetCurrentValue());
}

void UPlayerHUDWidget::HandleHealthChanged(float NewHealth)
{
	if (HealthBar && BoundHealthComponent.IsValid())
	{
		HealthBar->UpdateRatio(NewHealth, BoundHealthComponent->GetMaxValue());
	}
}
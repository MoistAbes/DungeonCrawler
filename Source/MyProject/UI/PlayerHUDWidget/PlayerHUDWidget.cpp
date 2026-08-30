

#include "PlayerHUDWidget.h"

void UPlayerHUDWidget::BindHealthComponent(UDamageableComponent* InDamageableComponent)
{
	if (!InDamageableComponent) return;

	// Odpięcie starego nasłuchu w przypadku ponownego bindowania
	if (BoundDamageableComponent.IsValid())
	{
		BoundDamageableComponent->OnHealthChanged.RemoveAll(this);
	}

	BoundDamageableComponent = InDamageableComponent;

	// Subskrypcja na strumień zdarzeń (Spring ApplicationEvent Listener)
	InDamageableComponent->OnHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);

	// Initial State Hydration: pobieramy stan początkowy przez kontrakt
	HandleHealthChanged(InDamageableComponent->GetCurrentValue());
}

void UPlayerHUDWidget::HandleHealthChanged(float NewHealth)
{
	if (HealthBar && BoundDamageableComponent.IsValid())
	{
		HealthBar->UpdateRatio(NewHealth, BoundDamageableComponent->GetMaxValue());
	}
}
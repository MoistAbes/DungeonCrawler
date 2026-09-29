#include "PlayerHUDWidget.h"
#include "Components/PanelWidget.h"

void UPlayerHUDWidget::BindHealthComponent(UDamageableComponent* InDamageableComponent)
{
	if (!InDamageableComponent) return;

	// Odpięcie starego nasłuchu w przypadku ponownego bindowania
	if (BoundDamageableComponent.IsValid())
	{
		BoundDamageableComponent->OnHealthChanged.RemoveAll(this);
	}

	BoundDamageableComponent = InDamageableComponent;
	InDamageableComponent->OnHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);

	// Initial State Hydration: pobieramy stan początkowy przez kontrakt
	HandleHealthChanged(InDamageableComponent->GetCurrentValue());
}

void UPlayerHUDWidget::BindStatusEffectComponent(UStatusEffectComponent* InStatusEffectComponent)
{
	if (!InStatusEffectComponent) return;

	if (BoundStatusEffectComponent.IsValid())
	{
		BoundStatusEffectComponent->OnStatusEffectApplied.RemoveAll(this);
		BoundStatusEffectComponent->OnStatusEffectRemoved.RemoveAll(this);
	}

	BoundStatusEffectComponent = InStatusEffectComponent;
	InStatusEffectComponent->OnStatusEffectApplied.AddDynamic(this, &UPlayerHUDWidget::HandleStatusEffectApplied);
	InStatusEffectComponent->OnStatusEffectRemoved.AddDynamic(this, &UPlayerHUDWidget::HandleStatusEffectRemoved);

	// Czyszczenie dotychczasowych ikonek
	if (StatusEffectsContainer)
	{
		StatusEffectsContainer->ClearChildren();
	}
	ActiveStatusIcons.Empty();

	// Hydratacja stanu początkowego (jeśli aktor już miał nałożone statusy)
	for (EStatusEffectType ActiveStatus : InStatusEffectComponent->GetActiveStatuses())
	{
		const float Remaining = InStatusEffectComponent->GetRemainingDuration(ActiveStatus);
		HandleStatusEffectApplied(ActiveStatus, Remaining);
	}
}

void UPlayerHUDWidget::HandleHealthChanged(float NewHealth)
{
	if (HealthBar && BoundDamageableComponent.IsValid())
	{
		HealthBar->UpdateRatio(NewHealth, BoundDamageableComponent->GetMaxValue());
	}
}

void UPlayerHUDWidget::HandleStatusEffectApplied(EStatusEffectType Status, float Duration)
{
	if (Status == EStatusEffectType::None) return;

	// Jeśli ikonka już istnieje w kontenerze (odświeżenie czasu trwania)
	if (TObjectPtr<UStatusEffectIconWidget>* FoundIcon = ActiveStatusIcons.Find(Status))
	{
		if (FoundIcon->Get())
		{
			(*FoundIcon)->SetupStatusIcon(Status, Duration);
		}
	}
	else if (StatusEffectsContainer && StatusIconWidgetClass)
	{
		// Tworzymy nową instancję ikony i dodajemy do kontenera pod paskiem HP
		if (UStatusEffectIconWidget* NewIcon = CreateWidget<UStatusEffectIconWidget>(this, StatusIconWidgetClass))
		{
			NewIcon->SetupStatusIcon(Status, Duration);
			StatusEffectsContainer->AddChild(NewIcon);
			ActiveStatusIcons.Add(Status, NewIcon);
		}
	}

	OnStatusEffectAdded(Status, Duration);
}

void UPlayerHUDWidget::HandleStatusEffectRemoved(EStatusEffectType Status)
{
	if (TObjectPtr<UStatusEffectIconWidget>* FoundIcon = ActiveStatusIcons.Find(Status))
	{
		if (StatusEffectsContainer && FoundIcon->Get())
		{
			StatusEffectsContainer->RemoveChild(*FoundIcon);
		}
		ActiveStatusIcons.Remove(Status);
	}

	OnStatusEffectRemoved(Status);
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Płynna aktualizacja odliczania czasu trwania dla aktywnych ikonek
	if (BoundStatusEffectComponent.IsValid() && ActiveStatusIcons.Num() > 0)
	{
		for (auto& Pair : ActiveStatusIcons)
		{
			if (Pair.Value)
			{
				const float Remaining = BoundStatusEffectComponent->GetRemainingDuration(Pair.Key);
				const float Total = BoundStatusEffectComponent->GetTotalDuration(Pair.Key);
				Pair.Value->UpdateDuration(Remaining, Total > 0.0f ? Total : Remaining);
			}
		}
	}
}

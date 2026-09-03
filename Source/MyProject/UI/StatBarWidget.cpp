#include "StatBarWidget.h"
#include "Components/ProgressBar.h"

UStatBarWidget::UStatBarWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UStatBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Jeśli mamy pasek Ghost i nie dogonił jeszcze głównego paska
	if (GhostProgressBar && !FMath::IsNearlyEqual(CurrentGhostPercent, TargetPercent, 0.001f))
	{
		// Jeśli leczenie (wartość wzrosła) -> snapujemy natychmiast
		if (CurrentGhostPercent < TargetPercent)
		{
			CurrentGhostPercent = TargetPercent;
		}
		else
		{
			// Jeśli obrażenia (wartość spadła) -> płynna interpolacja w dół
			CurrentGhostPercent = FMath::FInterpTo(CurrentGhostPercent, TargetPercent, InDeltaTime, GhostInterpSpeed);
		}

		GhostProgressBar->SetPercent(CurrentGhostPercent);
	}
}

void UStatBarWidget::UpdateRatio(float CurrentValue, float MaxValue)
{
	if (MaxValue <= 0.0f)
	{
		return;
	}

	TargetPercent = FMath::Clamp(CurrentValue / MaxValue, 0.0f, 1.0f);

	if (StatProgressBar)
	{
		StatProgressBar->SetPercent(TargetPercent);
	}

	// Jeśli pasek Ghost nie istnieje w widżecie lub to pierwsza inicjalizacja
	if (!GhostProgressBar)
	{
		CurrentGhostPercent = TargetPercent;
	}
	else if (CurrentGhostPercent < TargetPercent)
	{
		// Przy leczeniu pasek Ghost natychmiast rośnie razem z głównym
		CurrentGhostPercent = TargetPercent;
		GhostProgressBar->SetPercent(CurrentGhostPercent);
	}
}

void UStatBarWidget::SetBarColor(FLinearColor NewColor)
{
	if (StatProgressBar)
	{
		StatProgressBar->SetFillColorAndOpacity(NewColor);
	}
}

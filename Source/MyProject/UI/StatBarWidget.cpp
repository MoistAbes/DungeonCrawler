#include "StatBarWidget.h"
#include "Components/ProgressBar.h"

void UStatBarWidget::UpdateRatio(float CurrentValue, float MaxValue)
{
	if (StatProgressBar && MaxValue > 0.0f)
	{
		const float Ratio = FMath::Clamp(CurrentValue / MaxValue, 0.0f, 1.0f);
		StatProgressBar->SetPercent(Ratio);
	}
}

void UStatBarWidget::SetBarColor(FLinearColor NewColor)
{
	if (StatProgressBar)
	{
		StatProgressBar->SetFillColorAndOpacity(NewColor);
	}
}
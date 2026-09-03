#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StatBarWidget.generated.h"

class UProgressBar;

/**
 * Minimalistyczny pasek statystyki (np. zdrowia) z mechaniką Ghost Bar (opóźniony spadek po ciosie).
 */
UCLASS(Abstract)
class MYPROJECT_API UStatBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UStatBarWidget(const FObjectInitializer& ObjectInitializer);

	// API publiczne dla kontrolera widoku (Aktualizacja stanu paska)
	UFUNCTION(BlueprintCallable, Category = "UI|DataBinding")
	void UpdateRatio(float CurrentValue, float MaxValue);

	UFUNCTION(BlueprintCallable, Category = "UI|Style")
	void SetBarColor(FLinearColor NewColor);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Główny pasek postępu (natychmiastowa reakcja)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> StatProgressBar;

	// Opcjonalny pasek widmo (Ghost Bar) - płynnie dogania główny pasek po otrzymaniu obrażeń
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> GhostProgressBar;

	// Prędkość doganiania paska ghost (wyższa wartość = szybsze doganianie)
	UPROPERTY(EditDefaultsOnly, Category = "UI|Config", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float GhostInterpSpeed = 2.5f;

private:
	float TargetPercent = 1.0f;
	float CurrentGhostPercent = 1.0f;
};

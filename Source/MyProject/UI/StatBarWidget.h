#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StatBarWidget.generated.h"

class UProgressBar;

UCLASS(Abstract)
class MYPROJECT_API UStatBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// API publiczne dla kontrolera widoku (Aktualizacja stanu paska)
	UFUNCTION(BlueprintCallable, Category = "UI|DataBinding")
	void UpdateRatio(float CurrentValue, float MaxValue);

	UFUNCTION(BlueprintCallable, Category = "UI|Style")
	void SetBarColor(FLinearColor NewColor);

protected:
	// Wiązanie komponentu wizualnego z Blueprintem (Odpowiednik @FXML lub bindingu w szablonie)
	UPROPERTY(meta = (BindWidget))
	UProgressBar* StatProgressBar;
};
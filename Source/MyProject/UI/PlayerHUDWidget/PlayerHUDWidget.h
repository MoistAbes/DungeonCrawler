#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHUDWidget.generated.h"

class UStatBarWidget;
class UHealthComponent;

UCLASS(Abstract)
class MYPROJECT_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Wstrzyknięcie serwisu domenowego (Dependency Injection w warstwie prezentacji)
	UFUNCTION(BlueprintCallable, Category = "UI|Controller")
	void BindHealthComponent(UHealthComponent* InHealthComponent);

protected:
	// Wiązanie instancji reużywalnego paska z poziomu edytora UMG
	UPROPERTY(meta = (BindWidget))
	UStatBarWidget* HealthBar;

private:
	// Event Handler (Odpowiednik @EventListener w kontrolerze widoku)
	UFUNCTION()
	void HandleHealthChanged(float NewHealth);

	UPROPERTY()
	TWeakObjectPtr<UHealthComponent> BoundHealthComponent;
};
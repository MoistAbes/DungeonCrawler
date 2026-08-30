#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/UI/StatBarWidget.h"
#include "PlayerHUDWidget.generated.h"


UCLASS(Abstract)
class MYPROJECT_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Wstrzyknięcie serwisu domenowego (Dependency Injection w warstwie prezentacji)
	UFUNCTION(BlueprintCallable, Category = "UI|Controller")
	void BindHealthComponent(UDamageableComponent* InDamageableComponent);

protected:
	// Wiązanie instancji reużywalnego paska z poziomu edytora UMG
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UStatBarWidget> HealthBar;

private:
	// Event Handler (Odpowiednik @EventListener w kontrolerze widoku)
	UFUNCTION()
	void HandleHealthChanged(float NewHealth);

	UPROPERTY()
	TWeakObjectPtr<UDamageableComponent> BoundDamageableComponent;
};
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/UI/StatBarWidget.h"
#include "MyProject/UI/StatusIconWidget/StatusIconWidget.h"
#include "PlayerHUDWidget.generated.h"

class UPanelWidget;

UCLASS(Abstract)
class MYPROJECT_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Wstrzyknięcie serwisu zdrowia (Dependency Injection)
	UFUNCTION(BlueprintCallable, Category = "UI|Controller")
	void BindHealthComponent(UDamageableComponent* InDamageableComponent);

	// Wstrzyknięcie serwisu statusów żywiołowych
	UFUNCTION(BlueprintCallable, Category = "UI|Controller")
	void BindStatusEffectComponent(UStatusEffectComponent* InStatusEffectComponent);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Wiązanie instancji paska zdrowia
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UStatBarWidget> HealthBar;

	// Opcjonalny kontener na ikony statusów (np. HorizontalBox pod paskiem HP)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> StatusEffectsContainer;

	// Klasa widgetu pojedynczej ikony statusu (np. WBP_StatusIcon) do spawnowania w kontenerze
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Status")
	TSubclassOf<UStatusEffectIconWidget> StatusIconWidgetClass;

	// Zdarzenia powiadamiające Blueprint (jeśli użytkownik chce dodatkowo animować w UMG)
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Status|Events")
	void OnStatusEffectAdded(EStatusEffectType Status, float Duration);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Status|Events")
	void OnStatusEffectRemoved(EStatusEffectType Status);

private:
	// Event Handlery
	UFUNCTION()
	void HandleHealthChanged(float NewHealth);

	UFUNCTION()
	void HandleStatusEffectApplied(EStatusEffectType Status, float Duration);

	UFUNCTION()
	void HandleStatusEffectRemoved(EStatusEffectType Status);

	UPROPERTY()
	TWeakObjectPtr<UDamageableComponent> BoundDamageableComponent;

	UPROPERTY()
	TWeakObjectPtr<UStatusEffectComponent> BoundStatusEffectComponent;

	UPROPERTY()
	TMap<EStatusEffectType, TObjectPtr<UStatusEffectIconWidget>> ActiveStatusIcons;
};

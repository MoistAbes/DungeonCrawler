#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../../Shared/Interfaces/StatProviderInterface.h"
#include "UHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChangedSignature, float, NewHealth);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MYPROJECT_API UHealthComponent : public UActorComponent, public IStatProviderInterface
{
	GENERATED_BODY()

public:
	UHealthComponent();

	// Event Publisher (Spring ApplicationEventPublisher)
	UPROPERTY(BlueprintAssignable, Category = "Domain|Events")
	FOnHealthChangedSignature OnHealthChanged;

	// Kontrakt IStatProviderInterface (@Override)
	virtual float GetCurrentValue() const override { return CurrentHealth; }
	virtual float GetMaxValue() const override { return MaxHealth; }
	virtual float GetValueRatio() const override { return MaxHealth > 0.0f ? (CurrentHealth / MaxHealth) : 0.0f; }

protected:
	virtual void BeginPlay() override;

private:
	// Konfiguracja (application.yml)
	UPROPERTY(EditDefaultsOnly, Category = "Configuration|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Configuration|Health", meta = (ClampMin = "0.0"))
	float CurrentHealth = 75.0f;
};
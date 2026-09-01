#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Shared/Interfaces/StatProviderInterface.h"
#include "DamageableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChangedSignature, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDurabilityChangedSignature, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDestroyedSignature, AActor*, DestroyedActor);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MYPROJECT_API UDamageableComponent : public UActorComponent, public IStatProviderInterface
{
    GENERATED_BODY()

public:
    UDamageableComponent();

    // --- IStatProviderInterface ---
    virtual float GetCurrentValue() const override { return CurrentDurability; }
    virtual float GetMaxValue() const override { return MaxDurability; }
    virtual float GetValueRatio() const override { return MaxDurability > 0.0f ? (CurrentDurability / MaxDurability) : 0.0f; }

    // --- Domena ---
    UFUNCTION(BlueprintCallable, Category = "Combat|Damage")
    void ApplyDamage(float Amount);
    
    // Ujednolicone API: przyjmuje wyłącznie prędkość uderzenia w cm/s
    UFUNCTION(BlueprintCallable, Category = "Combat|Kinetic")
    void ApplyKineticImpact(float ImpactSpeed);

    // --- Stan ---
    UFUNCTION(BlueprintPure, Category = "Combat|State")
    float GetCurrentDurability() const { return CurrentDurability; }

    UFUNCTION(BlueprintPure, Category = "Combat|State")
    float GetMaxDurability() const { return MaxDurability; }

    UFUNCTION(BlueprintPure, Category = "Combat|State")
    bool IsDestroyed() const { return CurrentDurability <= 0.0f; }

    // --- Zdarzenia ---
    UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
    FOnHealthChangedSignature OnHealthChanged;

    UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
    FOnDurabilityChangedSignature OnDurabilityChanged;

    UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
    FOnDestroyedSignature OnDestroyed;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Durability", meta = (ClampMin = "1.0"))
    float MaxDurability = 100.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Durability", meta = (ClampMin = "0.0"))
    float InitialDurability = 100.0f;

    // Automatyczne nasłuchiwanie zderzeń i upadków dla postaci
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Kinetic")
    bool bAutoHandleCharacterImpacts = true;

    // Minimalna prędkość zderzenia generująca obrażenia (cm/s)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Kinetic", meta = (ClampMin = "0.0"))
    float ImpactSpeedThreshold = 700.0f;

    // Przelicznik nadmiarowej prędkości na obrażenia
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Kinetic", meta = (ClampMin = "0.0"))
    float ImpactDamageMultiplier = 0.05f;

private:
    UPROPERTY(VisibleInstanceOnly, Category = "State")
    float CurrentDurability = 100.0f;

    UFUNCTION()
    void HandleCharacterLanded(const FHitResult& Hit);

    UFUNCTION()
    void HandleCharacterHit(
        UPrimitiveComponent* HitComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        FVector NormalImpulse,
        const FHitResult& Hit);
};

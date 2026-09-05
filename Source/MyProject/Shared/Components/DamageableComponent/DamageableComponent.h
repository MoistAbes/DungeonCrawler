#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Shared/Interfaces/StatProviderInterface.h"
#include "DamageableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChangedSignature, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDurabilityChangedSignature, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDestroyedSignature, AActor*, DestroyedActor);

/**
 * Uniwersalny komponent integralności, punktów życia i wytrzymałości fizycznej (Durability).
 * Działa w architekturze Server-Authoritative First: modyfikacje stanu odbywają się wyłącznie
 * na serwerze, a stan jest replikowany do klientów za pomocą OnRep.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MYPROJECT_API UDamageableComponent : public UActorComponent, public IStatProviderInterface
{
    GENERATED_BODY()

public:
    UDamageableComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- IStatProviderInterface ---
    virtual float GetCurrentValue() const override { return CurrentDurability; }
    virtual float GetMaxValue() const override { return MaxDurability; }
    virtual float GetValueRatio() const override { return MaxDurability > 0.0f ? (CurrentDurability / MaxDurability) : 0.0f; }

    // --- Domena ---

    /** Aplikuje bezpośrednie obrażenia redukujące aktualną wytrzymałość/punkty życia (tylko na serwerze) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Durability")
    void ApplyDamage(float Amount);
    
    /** Aplikuje obrażenia kinetyczne na podstawie prędkości uderzenia (cm/s), uwzględniając próg i mnożnik (tylko na serwerze) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void ApplyKineticImpact(float ImpactSpeed);

    // --- Stan ---

    /** Zwraca aktualny stan punktów wytrzymałości/życia */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    float GetCurrentDurability() const { return CurrentDurability; }

    /** Zwraca maksymalny stan punktów wytrzymałości/życia */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    float GetMaxDurability() const { return MaxDurability; }

    /** Sprawdza, czy obiekt został całkowicie zniszczony (CurrentDurability <= 0) */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    bool IsDestroyed() const { return CurrentDurability <= 0.0f; }

    // --- Zdarzenia ---

    /** Wywoływane przy każdej zmianie aktualnego zdrowia/wytrzymałości */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnHealthChangedSignature OnHealthChanged;

    /** Wywoływane przy zmianie stanu (zwraca aktualne i maksymalne punkty) */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnDurabilityChangedSignature OnDurabilityChanged;

    /** Wywoływane w momencie, gdy punkty wytrzymałości spadną do zera */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnDestroyedSignature OnDestroyed;

protected:
    virtual void BeginPlay() override;

    /** Maksymalna liczba punktów wytrzymałości / zdrowia */
    UPROPERTY(ReplicatedUsing = OnRep_MaxDurability, EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Durability", meta = (ClampMin = "1.0"))
    float MaxDurability = 100.0f;

    /** Początkowa liczba punktów wytrzymałości przy spawnie obiektu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Durability", meta = (ClampMin = "0.0"))
    float InitialDurability = 100.0f;

    /** Automatyczne nasłuchiwanie zderzeń ze ścianami oraz upadków z wysokości dla postaci gracza/AI */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic")
    bool bAutoHandleCharacterImpacts = true;

    /** Minimalna prędkość zderzenia lub upadku generująca obrażenia kinetyczne (cm/s). Wszystko poniżej tej prędkości jest ignorowane */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "0.0"))
    float ImpactSpeedThreshold = 700.0f;

    /** Przelicznik nadmiarowej prędkości zderzenia na punkty obrażeń: Obrażenia = (Prędkość - Próg) * Mnożnik */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "0.0"))
    float ImpactDamageMultiplier = 0.05f;

    /** Reakcja na replikację maksymalnej wytrzymałości */
    UFUNCTION()
    void OnRep_MaxDurability(float OldMaxDurability);

    /** Reakcja na replikację aktualnej wytrzymałości z serwera */
    UFUNCTION()
    void OnRep_CurrentDurability(float OldDurability);

private:
    /** Aktualna wartość punktów wytrzymałości w czasie rzeczywistym (replikowana z serwera) */
    UPROPERTY(ReplicatedUsing = OnRep_CurrentDurability, VisibleInstanceOnly, Category = "Custom|Durability")
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

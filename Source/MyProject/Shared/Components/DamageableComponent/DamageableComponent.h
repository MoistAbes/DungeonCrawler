#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Shared/Interfaces/StatProviderInterface.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
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

    /** Aplikuje obrażenia redukujące wytrzymałość/punkty życia z uwzględnieniem typu i odporności (tylko na serwerze) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Durability")
    void ApplyDamage(float Amount, EDamageType DamageType = EDamageType::Physical, AActor* DamageCauser = nullptr);
    
    /** Aplikuje obrażenia kinetyczne na podstawie prędkości uderzenia (cm/s), uwzględniając próg i mnożnik (tylko na serwerze) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Kinetic")
    void ApplyKineticImpact(float ImpactSpeed);

    // --- Stan i Odporności ---

    /** Zwraca aktualny stan punktów wytrzymałości/życia */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    float GetCurrentDurability() const { return CurrentDurability; }

    /** Zwraca maksymalny stan punktów wytrzymałości/życia */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    float GetMaxDurability() const { return MaxDurability; }

    /** Sprawdza, czy obiekt został całkowicie zniszczony (CurrentDurability <= 0) */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    bool IsDestroyed() const { return CurrentDurability <= 0.0f; }

    /** Sprawdza, czy obiekt jest całkowicie niewrażliwy na obrażenia (Early Exit) */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    bool IsInvulnerable() const { return bIsInvulnerable; }

    /** Ustawia niewrażliwość na obrażenia (np. stabilne ściany i posadzki lochu) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Durability")
    void SetInvulnerable(bool bNewInvulnerable) { bIsInvulnerable = bNewInvulnerable; }

    /** Zwraca sumaryczną odporność na dany typ obrażeń (bazowa z materiału + modyfikatory postaci) */
    UFUNCTION(BlueprintPure, Category = "Custom|Durability")
    float GetTotalResistance(EDamageType DamageType) const;

    /** Ustawia lub modyfikuje dodatkową odporność dla postaci (np. zbroja, perki, buffy) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Durability")
    void SetResistanceModifier(EDamageType DamageType, float Modifier);

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

    /** Czy obiekt jest całkowicie niewrażliwy na jakiekolwiek obrażenia (Early Exit) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Custom|Durability")
    bool bIsInvulnerable = false;

    /** Opcjonalne dodatkowe modyfikatory odporności (np. zbroja postaci, buffy, perki) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Custom|Durability")
    TMap<EDamageType, float> ResistanceModifiers;

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

    /** Minimalny czas (w sekundach) między kolejnymi obrażeniami kinetycznymi. Zapobiega wielokrotnemu zadawaniu obrażeń w jednym zderzeniu (Double-Hit) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float KineticImpactCooldown = 0.25f;

    /** Reakcja na replikację maksymalnej wytrzymałości */
    UFUNCTION()
    void OnRep_MaxDurability(float OldMaxDurability);

    /** Reakcja na replikację aktualnej wytrzymałości z serwera */
    UFUNCTION()
    void OnRep_CurrentDurability(float OldDurability);

private:
    /** Czas świata ostatniego nałożenia obrażeń kinetycznych (do debounce) */
    double LastKineticImpactTime = -100.0;

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

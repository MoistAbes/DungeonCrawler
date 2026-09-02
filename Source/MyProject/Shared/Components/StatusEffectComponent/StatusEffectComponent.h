#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "StatusEffectComponent.generated.h"

class UDamageableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStatusEffectApplied, EStatusEffectType, StatusEffect, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStatusEffectRemoved, EStatusEffectType, StatusEffect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnElementalReactionTriggered, EStatusEffectType, IncomingStatus, EStatusEffectType, ExistingStatus, FName, ReactionTag);

/**
 * Pojedyncza instancja aktywnego statusu na obiekcie.
 */
USTRUCT(BlueprintType)
struct FActiveStatusEffectInstance
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    EStatusEffectType EffectType = EStatusEffectType::None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    float RemainingDuration = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    float TotalDuration = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    float TickInterval = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    float TimeUntilNextTick = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    TWeakObjectPtr<AActor> InstigatorActor = nullptr;
};

/**
 * Komponent zarządzający aktywnymi statusami żywiołowymi (Burning, Wet, Electrified, Oiled).
 * Odpowiedzialny wyłącznie za:
 * - Cykl życia i odliczanie czasu trwania nałożonych stanów
 * - Zadawanie okresowych obrażeń (DoT)
 * - Powiadamianie świata o zmianach stanu przez zdarzenia (delegaty)
 * 
 * Prawa chemii i reakcje delegowane są do UElementalChemistryLibrary.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UStatusEffectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UStatusEffectComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // -------------------------------------------------------------------------
    // API Domenowe
    // -------------------------------------------------------------------------

    /** Aplikuje status elementarny z podanym czasem trwania i sprawdzaniem reakcji */
    UFUNCTION(BlueprintCallable, Category = "StatusEffect")
    bool ApplyStatus(EStatusEffectType NewStatus, float Duration = 5.0f, AActor* InstigatorActor = nullptr);

    /** Usuwa aktywny status */
    UFUNCTION(BlueprintCallable, Category = "StatusEffect")
    bool RemoveStatus(EStatusEffectType StatusToRemove);

    /** Usuwa wszystkie aktywne statusy */
    UFUNCTION(BlueprintCallable, Category = "StatusEffect")
    void ClearAllStatuses();

    /** Czy dany status jest aktywny */
    UFUNCTION(BlueprintPure, Category = "StatusEffect")
    bool HasStatus(EStatusEffectType Status) const;

    /** Zwraca pozostały czas trwania danego statusu (0.0 jeśli brak) */
    UFUNCTION(BlueprintPure, Category = "StatusEffect")
    float GetRemainingDuration(EStatusEffectType Status) const;

    /** Zwraca całkowity czas trwania dla aktywnego statusu (0.0 jeśli brak) */
    UFUNCTION(BlueprintPure, Category = "StatusEffect")
    float GetTotalDuration(EStatusEffectType Status) const;

    /** Zwraca listę wszystkich aktualnie nałożonych statusów */
    UFUNCTION(BlueprintPure, Category = "StatusEffect")
    TArray<EStatusEffectType> GetActiveStatuses() const;

    // -------------------------------------------------------------------------
    // Delegaty Zdarzeń (Event-Driven Architecture)
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "StatusEffect|Events")
    FOnStatusEffectApplied OnStatusEffectApplied;

    UPROPERTY(BlueprintAssignable, Category = "StatusEffect|Events")
    FOnStatusEffectRemoved OnStatusEffectRemoved;

    UPROPERTY(BlueprintAssignable, Category = "StatusEffect|Events")
    FOnElementalReactionTriggered OnElementalReactionTriggered;

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Konfiguracja DoT (Data-Driven Defaults)
    // -------------------------------------------------------------------------

    /** Obrażenia na sekundę przy statusie Burning */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StatusEffect|Burning", meta = (ClampMin = "0.0"))
    float BurnDamagePerSecond = 5.0f;

    /** Co ile sekund status Burning aplikuje obrażenia */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StatusEffect|Burning", meta = (ClampMin = "0.1"))
    float BurnTickInterval = 1.0f;

private:
    /** Aktywne statusy indeksowane typem (gwarancja braku duplikatów) */
    UPROPERTY(VisibleInstanceOnly, Category = "StatusEffect|State")
    TMap<EStatusEffectType, FActiveStatusEffectInstance> ActiveEffects;

    /** Buforowana referencja do komponentu obrażeń */
    UPROPERTY()
    TObjectPtr<UDamageableComponent> DamageableComponent;

    // --- Metody pomocnicze ---
    EPhysicalMaterialType GetOwnerMaterialType() const;
    void UpdateTickState();
};

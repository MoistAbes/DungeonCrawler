#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
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

    /** Typ nałożonego statusu elementarnego */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    EStatusEffectType EffectType = EStatusEffectType::None;

    /** Czas pozostały do samoczynnego wygaśnięcia statusu (w sekundach) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float RemainingDuration = 0.0f;

    /** Całkowity czas, na jaki został zaaplikowany ten status */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float TotalDuration = 0.0f;

    /** Interwał (w sekundach), z jaką częstotliwością status wywołuje swój efekt okresowy (DoT) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float TickInterval = 1.0f;

    /** Czas do najbliższego uderzenia efektu okresowego */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float TimeUntilNextTick = 0.0f;

    /** Aktor, który zainicjował nałożenie tego statusu */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
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
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool ApplyStatus(EStatusEffectType NewStatus, float Duration = 5.0f, AActor* InstigatorActor = nullptr);

    /** Usuwa aktywny status z obiektu */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool RemoveStatus(EStatusEffectType StatusToRemove);

    /** Usuwa wszystkie aktywne statusy */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    void ClearAllStatuses();

    /** Czy dany status żywiołowy jest obecnie aktywny na tym obiekcie */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    bool HasStatus(EStatusEffectType Status) const;

    /** Zwraca pozostały czas trwania danego statusu (0.0 jeśli brak) */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    float GetRemainingDuration(EStatusEffectType Status) const;

    /** Zwraca całkowity czas trwania dla aktywnego statusu (0.0 jeśli brak) */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    float GetTotalDuration(EStatusEffectType Status) const;

    /** Zwraca listę wszystkich aktualnie nałożonych statusów */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    TArray<EStatusEffectType> GetActiveStatuses() const;

    // -------------------------------------------------------------------------
    // Delegaty Zdarzeń (Event-Driven Architecture)
    // -------------------------------------------------------------------------

    /** Wywoływane natychmiast po nałożeniu nowego statusu elementarnego */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnStatusEffectApplied OnStatusEffectApplied;

    /** Wywoływane po wygaśnięciu lub usunięciu statusu */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnStatusEffectRemoved OnStatusEffectRemoved;

    /** Wywoływane, gdy nałożenie statusu wywołało chemiczną reakcję żywiołów (np. Vaporize, Extinguish) */
    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnElementalReactionTriggered OnElementalReactionTriggered;

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Konfiguracja DoT (Data-Driven Defaults)
    // -------------------------------------------------------------------------

    /** Punkty obrażeń zadawane co sekundę w trakcie trwania statusu Burning (Podpalenie) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Status Effects|Burning", meta = (ClampMin = "0.0"))
    float BurnDamagePerSecond = 5.0f;

    /** Częstotliwość zadawania obrażeń przez status Burning (np. co 1.0 sekundy) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Status Effects|Burning", meta = (ClampMin = "0.1"))
    float BurnTickInterval = 1.0f;

    /** Czy renderować kolorowe etykiety debugowe 3D nad obiektem w świecie gry (nazwa statusu i czas) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Status Effects|Debug")
    bool bShowDebugInWorld = true;

private:
    /** Aktywne statusy indeksowane typem (gwarancja braku duplikatów) */
    UPROPERTY(VisibleInstanceOnly, Category = "Custom|Status Effects|State")
    TMap<EStatusEffectType, FActiveStatusEffectInstance> ActiveEffects;

    /** Buforowana referencja do komponentu obrażeń */
    UPROPERTY()
    TObjectPtr<UDamageableComponent> DamageableComponent;

    // --- Metody pomocnicze ---
    EPhysicalMaterialType GetOwnerMaterialType() const;
    void UpdateTickState();
};

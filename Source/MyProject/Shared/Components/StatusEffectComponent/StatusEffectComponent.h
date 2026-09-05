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
 * Zoptymalizowana pod Zero-Bandwidth Networking (tylko 9 bajtów w pakiecie sieciowym).
 */
USTRUCT(BlueprintType)
struct FActiveStatusEffectInstance
{
    GENERATED_BODY()

    /** Typ nałożonego statusu elementarnego */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    EStatusEffectType EffectType = EStatusEffectType::None;

    /** Całkowity czas, na jaki został zaaplikowany ten status (np. 5.0s) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float TotalDuration = 0.0f;

    /** Czas serwera (GetTimeSeconds), w którym status samoczynnie wygasa (Wzorzec Zero-Bandwidth) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float ServerEndTime = 0.0f;

    /** Interwał (w sekundach) DoT - przetwarzany wyłącznie lokalnie na serwerze */
    UPROPERTY(NotReplicated)
    float TickInterval = 1.0f;

    /** Czas do najbliższego uderzenia DoT - przetwarzany wyłącznie lokalnie na serwerze */
    UPROPERTY(NotReplicated)
    float TimeUntilNextTick = 0.0f;

    /** Aktor, który zainicjował nałożenie tego statusu */
    UPROPERTY(NotReplicated)
    TWeakObjectPtr<AActor> InstigatorActor = nullptr;

    bool operator==(const FActiveStatusEffectInstance& Other) const
    {
        return EffectType == Other.EffectType;
    }
};

/**
 * Replikowany komponent zarządzający aktywnymi statusami żywiołowymi (Burning, Wet, Electrified, Oiled).
 * Zaprojektowany pod kątem maksymalnej wydajności w kooperacji 1–6 graczy:
 * - Server-Authoritative: Tylko serwer nakłada/usuwa statusy i zadaje obrażenia DoT
 * - Zero-Bandwidth Timers: Replikowany ServerEndTime eliminuje pakietowy spam w klatkach
 * - Zero-Tick Idle: Komponent jest wygaszony gdy obiekt nie ma żadnych statusów
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UStatusEffectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UStatusEffectComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // -------------------------------------------------------------------------
    // API Domenowe (Server-Authoritative)
    // -------------------------------------------------------------------------

    /** Aplikuje status elementarny z podanym czasem trwania i sprawdzaniem reakcji (Tylko Serwer) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool ApplyStatus(EStatusEffectType NewStatus, float Duration = 5.0f, AActor* InstigatorActor = nullptr);

    /** Usuwa aktywny status z obiektu (Tylko Serwer) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool RemoveStatus(EStatusEffectType StatusToRemove);

    /** Usuwa wszystkie aktywne statusy (Tylko Serwer) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    void ClearAllStatuses();

    /** Czy dany status żywiołowy jest obecnie aktywny na tym obiekcie */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    bool HasStatus(EStatusEffectType Status) const;

    /** Zwraca pozostały czas trwania danego statusu w sekundach (obliczany on-demand z ServerEndTime) */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    float GetRemainingDuration(EStatusEffectType Status) const;

    /** Zwraca całkowity czas trwania dla aktywnego statusu (0.0 jeśli brak) */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    float GetTotalDuration(EStatusEffectType Status) const;

    /** Zwraca listę wszystkich aktualnie nałożonych typów statusów */
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
    /** 
     * Replikowana lista aktywnych instancji statusów.
     * Zastępuje TMap (niekompatybilne z UHT). Linear search na 1-4 elementach jest szybszy niż węzły mapy.
     */
    UPROPERTY(ReplicatedUsing = OnRep_ActiveStatusEffects, VisibleInstanceOnly, Category = "Custom|Status Effects|State")
    TArray<FActiveStatusEffectInstance> ActiveStatusEffects;

    /** Reaktywne powiadomienie klienta o zmianach w liście statusów */
    UFUNCTION()
    void OnRep_ActiveStatusEffects(const TArray<FActiveStatusEffectInstance>& OldEffects);

    /** Buforowana referencja do komponentu obrażeń */
    UPROPERTY()
    TObjectPtr<UDamageableComponent> DamageableComponent;

    // --- Metody pomocnicze ---
    const FActiveStatusEffectInstance* FindInstance(EStatusEffectType Status) const;
    FActiveStatusEffectInstance* FindInstanceMutable(EStatusEffectType Status);
    EPhysicalMaterialType GetOwnerMaterialType() const;
    void UpdateTickState();
    void DrawDebugLabels() const;
};

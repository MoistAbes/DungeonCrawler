#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
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

    /** Poziom/tier nałożonego statusu (0 = bazowy, 1 = silny, ...) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    int32 Tier = 0;

    /** Całkowity czas, na jaki został zaaplikowany ten status (np. 5.0s) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Status")
    float TotalDuration = 0.0f;

    /** Czas serwera (GetServerWorldTimeSeconds), w którym status samoczynnie wygasa (Wzorzec Zero-Bandwidth) */
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

    /** 
     * Aplikuje status elementarny z podanym tierem i opcjonalnym czasem trwania (Tylko Serwer).
     * Jeśli OverrideDuration <= 0.0f, czas trwania pobierany jest automatycznie z konfiguracji danego tieru (BaseDuration).
     */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool ApplyStatus(EStatusEffectType NewStatus, int32 Tier = 0, float OverrideDuration = -1.0f, AActor* InstigatorActor = nullptr);

    /** Usuwa aktywny status z obiektu (Tylko Serwer) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool RemoveStatus(EStatusEffectType StatusToRemove);

    /** Usuwa z obiektu statusy, które bez swoich nośników nie mogą dłużej legalnie istnieć na tym materiale (Tylko Serwer) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    bool CleanOrphanedStatuses();

    /** Usuwa wszystkie aktywne statusy (Tylko Serwer) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Status Effects")
    void ClearAllStatuses();

    /** Czy dany status żywiołowy jest obecnie aktywny na tym obiekcie */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    bool HasStatus(EStatusEffectType Status) const;

    /** Zwraca tier danego aktywnego statusu (0 jeśli brak lub bazowy) */
    UFUNCTION(BlueprintPure, Category = "Custom|Status Effects")
    int32 GetStatusTier(EStatusEffectType Status) const;

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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

    // --- Metody pomocnicze (Single Responsibility) ---
    const FActiveStatusEffectInstance* FindInstance(EStatusEffectType Status) const;
    FActiveStatusEffectInstance* FindInstance(EStatusEffectType Status);

    EPhysicalMaterialType GetOwnerMaterialType() const;
    float GetCurrentSyncedTime() const;
    void UpdateTickState();
    void DrawDebugLabels() const;

    /** Przetwarza potencjalną reakcję chemiczną żywiołów. Zwraca wynik ewaluacji reakcji */
    FElementalReactionResult ProcessElementalReaction(EStatusEffectType NewStatus, const TArray<EStatusEffectType>& ActiveStatuses);

    /** Wylicza ostateczny czas trwania statusu z uwzględnieniem fizycznych nośników obecnych na obiekcie */
    float ComputeAdjustedDuration(EStatusEffectType Status, float BaseDuration, bool bSyncWithCarrier) const;

    /** Egzekwuje wyłączność płynów: usuwa wszelkie inne ciecze aktywne na obiekcie */
    void DisplaceOtherLiquids(EStatusEffectType IncomingLiquid);

    /** Synchronizuje czas trwania statusów zależnych, gdy nośnik został dodany lub przedłużony */
    void SyncDependentStatusesWithCarrier(EStatusEffectType CarrierStatus, float CarrierEndTime);

    /** Dodaje nową instancję statusu lub odświeża istniejącą (Upsert) */
    void UpsertStatus(EStatusEffectType Status, int32 Tier, float Duration, float EndTime, AActor* InstigatorActor);

    /** Odświeża czas trwania i parametry istniejącego statusu */
    void RefreshExistingStatus(FActiveStatusEffectInstance& Existing, int32 Tier, float Duration, float NewEndTime, AActor* InstigatorActor);

    /** Tworzy i rejestruje nową instancję statusu na podstawie danych z rejestru */
    void AddNewStatusInstance(EStatusEffectType NewStatus, int32 Tier, float Duration, float NewEndTime, AActor* InstigatorActor);

    /** Flaga chroniąca przed zapętleniem rekurencji podczas czyszczenia osieroconych statusów */
    bool bIsCleaningOrphans = false;
};

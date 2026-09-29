#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "StatusIconWidget.generated.h"

class UImage;
class UTextBlock;
class UMaterialInstanceDynamic;

/**
 * Reużywalny widget pojedynczej ikony aktywnego statusu w UI (np. Burning, Wet, Electrified, Oiled).
 * Zaprojektowany pod minimalistyczny okrągły badge z kurczącą się krawędzią czasu trwania w materiale.
 */
UCLASS(Abstract)
class MYPROJECT_API UStatusEffectIconWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UStatusEffectIconWidget(const FObjectInitializer& ObjectInitializer);

    /** Inicjalizuje ikonę dla danego typu statusu i czasu trwania */
    UFUNCTION(BlueprintCallable, Category = "UI|Status")
    virtual void SetupStatusIcon(EStatusEffectType InType, float InDuration);

    /** Aktualizuje czas trwania (wyliczając ułamek Remaining / Total) */
    UFUNCTION(BlueprintCallable, Category = "UI|Status")
    virtual void UpdateDuration(float RemainingTime, float TotalTime);

    /** Zwraca typ statusu reprezentowany przez ten widget */
    UFUNCTION(BlueprintPure, Category = "UI|Status")
    EStatusEffectType GetStatusEffectType() const { return StatusType; }

    /** Zwraca kolor żywiołu (z mapy StatusColors lub domyślny) */
    UFUNCTION(BlueprintPure, Category = "UI|Status")
    FLinearColor GetStatusColor(EStatusEffectType InType) const;

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "UI|Status")
    EStatusEffectType StatusType = EStatusEffectType::None;

    /** Przypisanie tekstur dla poszczególnych żywiołów (ustawiane w Blueprint Defaults) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Config")
    TMap<EStatusEffectType, TObjectPtr<UTexture2D>> StatusTextures;

    /** Konfigurowalna paleta barw dla żywiołów (ustawiana w Blueprint Defaults) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Config")
    TMap<EStatusEffectType, FLinearColor> StatusColors;

    /** Obrazek ikony (tekstura żywiołu) */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> IconImage;

    /** Zewnętrzna obwódka / pierścień czasu (Dynamic Material ze skalarem "Percent") */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> BorderCooldownImage;

    /** Opcjonalny tekst sekund (jeśli ktoś doda do szablonu) */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> DurationText;

    /** Event dla Blueprinta do customowych animacji (np. pulsowanie, dźwięk) */
    UFUNCTION(BlueprintImplementableEvent, Category = "UI|Status")
    void OnStatusInitialized(EStatusEffectType InType, float InDuration, FLinearColor DefaultColor);

private:
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> DynamicCooldownMaterial;

    void EnsureDynamicMaterial();
};

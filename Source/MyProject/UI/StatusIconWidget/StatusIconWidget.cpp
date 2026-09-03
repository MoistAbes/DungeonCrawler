#include "StatusIconWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

void UStatusEffectIconWidget::NativeConstruct()
{
    Super::NativeConstruct();
    EnsureDynamicMaterial();
}

void UStatusEffectIconWidget::EnsureDynamicMaterial()
{
    // Jeśli mamy dedykowany BorderCooldownImage lub IconImage z materiałem
    UImage* TargetImage = BorderCooldownImage ? BorderCooldownImage.Get() : IconImage.Get();
    if (TargetImage && !DynamicCooldownMaterial)
    {
        DynamicCooldownMaterial = TargetImage->GetDynamicMaterial();
    }
}

void UStatusEffectIconWidget::SetupStatusIcon(EStatusEffectType InType, float InDuration)
{
    StatusType = InType;
    EnsureDynamicMaterial();

    const FLinearColor StatusColor = GetDefaultStatusColor(InType);

    // 1. Podpięcie dedykowanej tekstury z mapy
    bool bHasCustomTexture = false;
    if (TObjectPtr<UTexture2D>* FoundTex = StatusTextures.Find(InType))
    {
        if (FoundTex && FoundTex->Get() && IconImage)
        {
            IconImage->SetBrushFromTexture(FoundTex->Get());
            bHasCustomTexture = true;
        }
    }

    // Jeśli mamy dedykowaną teksturę graficzną, zachowujemy jej oryginalne barwy (White).
    // Jeśli brak dedykowanej grafiki (np. generyczna maska), barwimy domyślnym kolorem żywiołu.
    if (IconImage)
    {
        IconImage->SetColorAndOpacity(bHasCustomTexture ? FLinearColor::White : StatusColor);
    }

    // 2. Kolorowanie i reset parametru materiału obwódki
    if (DynamicCooldownMaterial)
    {
        DynamicCooldownMaterial->SetVectorParameterValue(TEXT("Color"), StatusColor);
        DynamicCooldownMaterial->SetScalarParameterValue(TEXT("Percent"), 1.0f);
    }

    if (BorderCooldownImage && !DynamicCooldownMaterial)
    {
        BorderCooldownImage->SetColorAndOpacity(StatusColor);
    }

    // 3. Tekst
    if (DurationText)
    {
        DurationText->SetText(FText::FromString(FString::Printf(TEXT("%.0fs"), InDuration)));
    }

    // 4. Ewentualny progress bar
    if (DurationProgressBar)
    {
        DurationProgressBar->SetPercent(1.0f);
        DurationProgressBar->SetFillColorAndOpacity(StatusColor);
    }

    OnStatusInitialized(InType, InDuration, StatusColor);
}

void UStatusEffectIconWidget::UpdateDuration(float RemainingTime, float TotalTime)
{
    const float Ratio = (TotalTime > 0.0f) ? FMath::Clamp(RemainingTime / TotalTime, 0.0f, 1.0f) : 0.0f;

    // Płynne zwężanie obwódki radialnej w materiale (Percent: 1.0 -> 0.0)
    if (DynamicCooldownMaterial)
    {
        DynamicCooldownMaterial->SetScalarParameterValue(TEXT("Percent"), Ratio);
    }

    if (DurationText)
    {
        DurationText->SetText(FText::FromString(FString::Printf(TEXT("%.0fs"), FMath::Max(0.0f, RemainingTime))));
    }

    if (DurationProgressBar)
    {
        DurationProgressBar->SetPercent(Ratio);
    }
}

FLinearColor UStatusEffectIconWidget::GetDefaultStatusColor(EStatusEffectType InType)
{
    switch (InType)
    {
    case EStatusEffectType::Burning:
        return FLinearColor(1.0f, 0.35f, 0.05f, 1.0f); // Ognisty pomarańcz/czerwień
    case EStatusEffectType::Wet:
        return FLinearColor(0.15f, 0.7f, 1.0f, 1.0f);  // Wodny wyrazisty błękit
    case EStatusEffectType::Electrified:
        return FLinearColor(1.0f, 0.85f, 0.1f, 1.0f);  // Piorunowy wyrazisty żółty
    case EStatusEffectType::Oiled:
        return FLinearColor(0.7f, 0.25f, 0.95f, 1.0f); // Oleisty fiolet
    default:
        return FLinearColor::White;
    }
}

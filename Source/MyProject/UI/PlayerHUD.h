#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PlayerHUD.generated.h"

class UPlayerHUDWidget;
class UDamageableComponent;

/**
 * Główny aktor HUD dla gracza.
 * Zarządza cyklem życia widgetów UI i wiązaniem ich ze stanem kontrolowanego Pawna.
 */
UCLASS()
class MYPROJECT_API APlayerHUD : public AHUD
{
    GENERATED_BODY()

public:
    APlayerHUD();

    /** Inicjalizuje lub rekonfiguruje UI z nowo kontrolowanym Pawnem */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void BindToPawn(APawn* InPawn);

    UFUNCTION(BlueprintPure, Category = "UI")
    UPlayerHUDWidget* GetHUDWidget() const { return HUDWidget; }

protected:
    virtual void BeginPlay() override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

private:
    UPROPERTY()
    TObjectPtr<UPlayerHUDWidget> HUDWidget;

    void CreateHUDWidget();

    UFUNCTION()
    void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
};

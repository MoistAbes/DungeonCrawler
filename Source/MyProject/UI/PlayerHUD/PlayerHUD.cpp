#include "PlayerHUD.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/UI/PlayerHUDWidget/PlayerHUDWidget.h"
#include "UObject/ConstructorHelpers.h"

APlayerHUD::APlayerHUD()
{
    static ConstructorHelpers::FClassFinder<UPlayerHUDWidget> HUDWidgetFinder(
        TEXT("/Game/UI/WBP_PlayerHUD.WBP_PlayerHUD_C"));

    if (HUDWidgetFinder.Succeeded())
    {
        HUDWidgetClass = HUDWidgetFinder.Class;
    }
}

void APlayerHUD::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = GetOwningPlayerController())
    {
        PC->OnPossessedPawnChanged.AddDynamic(
            this,
            &APlayerHUD::HandlePossessedPawnChanged);
    }

    CreateHUDWidget();
}

void APlayerHUD::CreateHUDWidget()
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !PC->IsLocalController())
    {
        return;
    }

    if (HUDWidgetClass && !HUDWidget)
    {
        HUDWidget = CreateWidget<UPlayerHUDWidget>(PC, HUDWidgetClass);
        if (HUDWidget)
        {
            HUDWidget->AddToViewport();
        }
    }

    if (PC->GetPawn())
    {
        BindToPawn(PC->GetPawn());
    }
}

void APlayerHUD::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
    if (NewPawn)
    {
        if (!HUDWidget)
        {
            CreateHUDWidget();
        }
        else
        {
            BindToPawn(NewPawn);
        }
    }
}

void APlayerHUD::BindToPawn(APawn* InPawn)
{
    if (!InPawn || !HUDWidget)
    {
        return;
    }

    if (UDamageableComponent* Damageable = InPawn->FindComponentByClass<UDamageableComponent>())
    {
        HUDWidget->BindHealthComponent(Damageable);
    }
}

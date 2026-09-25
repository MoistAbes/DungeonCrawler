#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DungeonElementalSettings.generated.h"

class UStatusEffectConfigAsset;

/**
 * Ustawienia globalne systemów żywiołowych lochu.
 * Dostępne w edytorze: Project Settings -> Game -> Dungeon Elemental Settings.
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "Dungeon Elemental Settings"))
class MYPROJECT_API UDungeonElementalSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDungeonElementalSettings();

	/** Główny DataAsset z tierami i konfiguracją statusów */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Status Configuration")
	TSoftObjectPtr<UStatusEffectConfigAsset> StatusEffectConfigAsset;
};

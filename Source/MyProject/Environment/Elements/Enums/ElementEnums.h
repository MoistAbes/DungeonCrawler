#pragma once

#include "CoreMinimal.h"
#include "ElementEnums.generated.h"

UENUM(BlueprintType)
enum class EStatusEffectType : uint8
{
	None         UMETA(DisplayName = "None"),
	Burning      UMETA(DisplayName = "Burning"),
	Wet          UMETA(DisplayName = "Wet"),
	Electrified  UMETA(DisplayName = "Electrified"),
	Oiled        UMETA(DisplayName = "Oiled")
};

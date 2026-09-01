#pragma once

#include "CoreMinimal.h"
#include "CombatEnums.generated.h"

UENUM(BlueprintType)
enum class EPhysicalMaterialType : uint8
{
	Default  UMETA(DisplayName = "Default"),
	Glass    UMETA(DisplayName = "Glass"),
	Wood     UMETA(DisplayName = "Wood"),
	Stone    UMETA(DisplayName = "Stone"),
	Metal    UMETA(DisplayName = "Metal"),
	Flesh    UMETA(DisplayName = "Flesh")
};

UENUM(BlueprintType)
enum class EStatusEffectType : uint8
{
	None         UMETA(DisplayName = "None"),
	Burning      UMETA(DisplayName = "Burning"),
	Wet          UMETA(DisplayName = "Wet"),
	Electrified  UMETA(DisplayName = "Electrified"),
	Oiled        UMETA(DisplayName = "Oiled")
};

UENUM(BlueprintType)
enum class EKnockbackFalloff : uint8
{
	Linear   UMETA(DisplayName = "Linear"),
	Constant UMETA(DisplayName = "Constant")
};

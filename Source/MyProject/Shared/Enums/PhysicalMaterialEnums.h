#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterialEnums.generated.h"

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

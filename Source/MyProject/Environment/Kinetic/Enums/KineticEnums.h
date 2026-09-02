#pragma once

#include "CoreMinimal.h"
#include "KineticEnums.generated.h"

UENUM(BlueprintType)
enum class EKnockbackFalloff : uint8
{
	Linear   UMETA(DisplayName = "Linear"),
	Constant UMETA(DisplayName = "Constant")
};

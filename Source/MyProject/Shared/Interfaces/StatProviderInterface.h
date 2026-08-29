#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StatProviderInterface.generated.h"

// Klasa metadanych silnika (odpowiednik pakietu metadanych refleksji JVM)
UINTERFACE(MinimalAPI, BlueprintType)
class UStatProviderInterface : public UInterface
{
	GENERATED_BODY()
};

// Właściwy kontrakt domenowy C++ (odpowiednik public interface w Javie)
class MYPROJECT_API IStatProviderInterface
{
	GENERATED_BODY()

public:
	virtual float GetCurrentValue() const = 0;
	virtual float GetMaxValue() const = 0;
	virtual float GetValueRatio() const = 0;
};
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CarryAnchorProviderInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UCarryAnchorProviderInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Kontrakt dla postaci/aktorów posiadających możliwość noszenia obiektów fizycznych
 * za pomocą UInteractionComponent (gracz, humanoid AI, potwory itp.).
 */
class MYPROJECT_API ICarryAnchorProviderInterface
{
	GENERATED_BODY()

public:
	/** Komponent kotwicy (Hold Anchor), za którym podąża niesiony prop */
	virtual USceneComponent* GetHoldAnchorComponent() const = 0;

	/** Bazowy offset wysokości wzroku postaci (używany do kalkulacji nachylenia kotwicy) */
	virtual float GetCarryEyeHeightOffset() const { return 65.0f; }

	/** Maksymalna masa pojedynczego propa (kg), jaką postać może pchnąć przy kolizji */
	virtual float GetMaxPushableMass() const { return 100.0f; }

	/** Siła fizycznego pchania w niutonach (N) przekazywana na lżejsze przeszkody */
	virtual float GetPlayerPushForce() const { return 150000.0f; }
};

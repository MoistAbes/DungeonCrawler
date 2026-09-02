#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "ExplosiveBarrelProp.generated.h"

/**
 * Wybuchająca beczka fizyczna w lochu.
 * Po zniszczeniu (spadek HP do 0 / silne zderzenie) detonuje wybuch z użyciem UKineticForceLibrary.
 */
UCLASS()
class MYPROJECT_API AExplosiveBarrelProp : public AInteractivePropBase
{
	GENERATED_BODY()

public:
	AExplosiveBarrelProp();

protected:
	virtual void HandleOnDestroyed(AActor* DestroyedActor) override;

	// -------------------------------------------------------------------------
	// Parametry Wybuchu
	// -------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosion|Config", meta = (ClampMin = "50.0"))
	float ExplosionRadius = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosion|Config", meta = (ClampMin = "0.0"))
	float ExplosionDamage = 25.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosion|Config", meta = (ClampMin = "0.0"))
	float ExplosionKnockbackForce = 1800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosion|Config")
	bool bDrawDebugExplosion = true;

private:
	bool bHasExploded = false;
};

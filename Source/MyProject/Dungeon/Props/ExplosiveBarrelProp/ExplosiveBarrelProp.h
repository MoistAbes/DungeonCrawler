#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Props/VolatileProp/VolatileProp.h"
#include "ExplosiveBarrelProp.generated.h"

/**
 * Wybuchająca beczka fizyczna w lochu (specjalizacja AVolatileProp).
 * Zachowuje wsteczną kompatybilność z istniejącymi Blueprintami (BP_ExplosiveBarrel).
 */
UCLASS()
class MYPROJECT_API AExplosiveBarrelProp : public AVolatileProp
{
	GENERATED_BODY()

public:
	AExplosiveBarrelProp();
};

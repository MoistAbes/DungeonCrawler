#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "VolumetricStatusZone.generated.h"

/**
 * Specjalizacja strefy statusu dla pełnych brył objętościowych w powietrzu (Volumetric Sphere).
 * Dedykowana dla: chmur trującego gazu, dymu, mgły parowej, stref ciszy.
 * Nie posiada projektora Decal ani kalkulacji obrysu 2D – maksymalna wydajność pamięciowa i sieciowa.
 */
UCLASS()
class MYPROJECT_API AVolumetricStatusZone : public AStatusZoneBase
{
	GENERATED_BODY()

public:
	AVolumetricStatusZone();

	/** Inicjalizuje przestrzenną strefę sferyczną w 3D */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void InitializeVolumetricZone(
		const FZoneEffectConfig& InConfig,
		float InRadius,
		float InDuration,
		AActor* InInstigator = nullptr);

protected:
	virtual bool IsActorWithinZoneGeometry(const FBoxSphereBounds& Bounds) const override;
	virtual void DrawDebugVisuals() const override;
};

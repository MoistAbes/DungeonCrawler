#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "MyProject/Environment/Zones/Shapes/SurfaceSplashZone.h"
#include "MyProject/Environment/Zones/Shapes/VolumetricStatusZone.h"
#include "StatusZone.generated.h"

/**
 * Klasa zachowująca wsteczną kompatybilność dla projektu.
 * Dziedziczy po ASurfaceSplashZone, delegując całą logikę do modularnej hierarchii stref.
 */
UCLASS()
class MYPROJECT_API AStatusZone : public ASurfaceSplashZone
{
	GENERATED_BODY()

public:
	AStatusZone() {}

	/** Przekierowanie inicjalizacji dla wstecznej kompatybilności */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void InitializeZone(
		const FZoneEffectConfig& InConfig,
		float InRadius,
		float InDuration,
		EZoneShapeType InShapeType = EZoneShapeType::SurfaceSplash,
		float InSurfaceHeight = 35.0f,
		const FVector& InSurfaceNormal = FVector(0.0f, 0.0f, 1.0f),
		AActor* InInstigator = nullptr)
	{
		InitializeSurfaceSplash(InConfig, InRadius, InDuration, InSurfaceHeight, InSurfaceNormal, InInstigator);
	}
};

#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "SurfaceSplashZone.generated.h"

class UDecalComponent;

/**
 * Specjalizacja strefy statusu dla cienkich powłok powierzchniowych (Surface Splash).
 * Przylega do fundamentów lochu (ściany pionowe, posadzki poziome, rampy, sufity).
 * Zawiera projektor Decal, 48-promieniowy obrys z wykrywaniem krawędzi (Drop-Off Test)
 * oraz precyzyjne przycinanie do architektury.
 */
UCLASS()
class MYPROJECT_API ASurfaceSplashZone : public AStatusZoneBase
{
	GENERATED_BODY()

public:
	ASurfaceSplashZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Inicjalizuje strefę powierzchniową z określoną orientacją i wysokością powłoki */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void InitializeSurfaceSplash(
		const FZoneEffectConfig& InConfig,
		float InRadius,
		float InDuration,
		float InSurfaceHeight,
		const FVector& InSurfaceNormal,
		AActor* InInstigator = nullptr);

	/** Przelicza punkty obrysu z uwzględnieniem przeszkód i krawędzi posadzki/ściany */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void RebuildPerimeterPoints();

	/** Zwraca wektor normalny powierzchni, na której spoczywa plama */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	const FVector& GetSurfaceNormal() const { return SurfaceNormal; }

	/** Zwraca dopuszczalną grubość powłoki cieczy */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	float GetSurfaceHeight() const { return SurfaceHeight; }

	/** Wylicza efektywną grubość strefy dla aktualnego statusu (np. 85 cm dla ognia, SurfaceHeight dla cieczy) */
	float GetMaxAllowedHeight() const;

	/** Zwraca maksymalny promień strefy na zadanym kącie obrysu (uwzględnia przeszkody i wycięcia) */
	float GetPerimeterRadiusAtAngle(float AngleRad) const;

protected:
	virtual void BeginPlay() override;

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Projektor Decal rzutujący teksturę cieczy/ognia/kwasu na ścianę lub podłogę */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDecalComponent> ZoneDecal;

	// -------------------------------------------------------------------------
	// Replikacja sieciowa
	// -------------------------------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_SurfaceNormal)
	FVector SurfaceNormal = FVector(0.0f, 0.0f, 1.0f);

	UPROPERTY(ReplicatedUsing = OnRep_SurfaceHeight)
	float SurfaceHeight = 35.0f;

	virtual void OnRep_Radius() override;

	UFUNCTION()
	virtual void OnRep_SurfaceNormal();

	UFUNCTION()
	virtual void OnRep_SurfaceHeight();

	// -------------------------------------------------------------------------
	// Implementacja metod wirtualnych AStatusZoneBase
	// -------------------------------------------------------------------------

	virtual bool IsActorWithinZoneGeometry(const FBoxSphereBounds& Bounds) const override;
	virtual void DrawDebugVisuals() const override;
	virtual float CalculateBroadphaseRadius() const override;
	virtual bool CanZonesInteract(const AStatusZoneBase* OtherZone) const override;
	virtual bool HandleLiquidDisplacement(AActor* HitInstigator) override;

private:
	/** Punkty obrysu z uwzględnieniem Line-of-Sight ścian i krawędzi architektury */
	mutable TArray<FVector> CachedPerimeterPoints;
};

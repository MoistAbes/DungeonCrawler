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

	/** Sprawdza, czy pamięć podręczna obrysu jest aktualna względem aktualnego położenia, orientacji i promienia */
	bool IsPerimeterCacheValid() const;

	/** Sprawdza, czy zadany punkt i wektor normalny leżą w tej samej płaszczyźnie co ta strefa */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	bool IsCoplanarWithPoint(const FVector& OtherLocation, const FVector& OtherNormal, float ToleranceDist = 30.0f, float MinDot = 0.85f) const;

	/** Sprawdza, czy inna strefa powierzchniowa leży w tej samej płaszczyźnie */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	bool IsCoplanarWithZone(const ASurfaceSplashZone* OtherSplash, float ToleranceDist = 30.0f, float MinDot = 0.85f) const;

	/** Wygodny alias C++ do sprawdzania płaszczyzny punktu */
	FORCEINLINE bool IsCoplanarWith(const FVector& OtherLocation, const FVector& OtherNormal, float ToleranceDist = 30.0f, float MinDot = 0.85f) const
	{
		return IsCoplanarWithPoint(OtherLocation, OtherNormal, ToleranceDist, MinDot);
	}

	/** Wygodny alias C++ do sprawdzania płaszczyzny innej strefy */
	FORCEINLINE bool IsCoplanarWith(const ASurfaceSplashZone* OtherSplash, float ToleranceDist = 30.0f, float MinDot = 0.85f) const
	{
		return IsCoplanarWithZone(OtherSplash, ToleranceDist, MinDot);
	}

	virtual void MergeWithZone(float InDuration, float RadiusGrowthMultiplier = 1.20f, float MaxRadiusCap = 1000.0f) override;

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

	/** Aktualizuje rozmiar i orientację projektora Decal */
	void UpdateDecalTransform();

private:
	// -------------------------------------------------------------------------
	// Pomocnicze metody obliczeniowe (obrys, kolizja, geometria)
	// -------------------------------------------------------------------------

	/** Weryfikuje, czy w zadanym punkcie w przestrzeni istnieje stabilne podłoże (podłoga/ściana) */
	bool CheckSurfacePresentAt(const FVector& ProbeCenter, float LiftOffset, const FCollisionQueryParams& TraceParams) const;

	/** Sprawdza zasięg w danym kierunku radialnym z uwzględnieniem pionowych przeszkód */
	float TraceObstacleDistance(const FVector& Center, const FVector& TraceStart, const FVector& RayDir, float MaxDist, const FCollisionQueryParams& TraceParams) const;

	/** Szuka dokładnej krawędzi podłoża metodą binary search (Drop-Off Test) */
	float FindDropOffEdgeDistance(const FVector& Center, const FVector& RayDir, float InitialMaxDist, float LiftOffset, const FCollisionQueryParams& TraceParams) const;

	/** Sprawdza, czy obiekt mieści się w granicach grubości powłoki wzdłuż wektora normalnego */
	bool IsWithinNormalBounds(const FBoxSphereBounds& Bounds, float& OutDistNormal) const;

	/** Sprawdza, czy obiekt mieści się w granicach wielokąta obrysu w płaszczyźnie stycznej */
	bool IsWithinTangentialPerimeter(const FBoxSphereBounds& Bounds, float DistNormal) const;

	/** Punkty obrysu w przestrzeni świata */
	mutable TArray<FVector> CachedPerimeterPoints;

	/** Promienie obrysu dla 48 kierunków radialnych (do szybkiej interpolacji) */
	mutable TArray<float> CachedPerimeterDistances;

	/** Parametry transformu i promienia, dla których wyliczono powyższy cache */
	mutable FVector CachedCenter = FVector(NAN);
	mutable FVector CachedNormal = FVector(NAN);
	mutable float CachedRadius = -1.0f;
};

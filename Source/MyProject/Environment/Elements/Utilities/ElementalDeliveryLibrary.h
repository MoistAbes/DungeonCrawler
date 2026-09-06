#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/HitResult.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Environment/Elements/StatusZone/ElementalStatusZone.h"
#include "ElementalDeliveryLibrary.generated.h"

class AElementalStatusZone;

/**
 * Główny, zunifikowany silnik dostarczania żywiołów do świata gry (Elemental Delivery Engine).
 * Odpowiedzialny za 4 fundamentalne archetypy rozprzestrzeniania statusów:
 * 1. Point Hit: bezpośrednie trafienie pojedynczym pociskiem/strzałą/bełtem w cel.
 * 2. Surface Splash: płaski rozbryzg ze szklanej fiolki/butelki na konkretną powierzchnię (ściana/podłoga).
 * 3. Radial Burst: kulista fala uderzeniowa z line-of-sight (granaty, bomby, wybuchy prochu).
 * 4. Status Zone: trwała strefa/kałuża/pożar w czasie z reakcjami łańcuchowymi.
 */
UCLASS()
class MYPROJECT_API UElementalDeliveryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 1. POINT HIT (Uderzenie punktowe)
	 * Trafienie pojedynczym pociskiem w 1 obiekt (np. ognista strzała, water bolt, pochodnia).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Elements|Delivery", meta = (DefaultToSelf = "InstigatorActor"))
	static bool ApplyPointHit(
		AActor* TargetActor,
		const FVector& HitLocation,
		const FVector& HitNormal,
		EStatusEffectType StatusType,
		float Duration,
		AActor* InstigatorActor = nullptr);

	/**
	 * 2. SURFACE SPLASH (Płaski rozbryzg z fiolki / butelki)
	 * Uderzenie szklanego naczynia w powierzchnię (płaska plama na 1 ścianie lub kałużka na podłodze).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Elements|Delivery", meta = (WorldContext = "WorldContextObject"))
	static AElementalStatusZone* ApplySurfaceSplash(
		const UObject* WorldContextObject,
		const FHitResult& HitResult,
		float SplashRadius,
		EStatusEffectType StatusType,
		float Duration,
		AActor* InstigatorActor = nullptr);

	/**
	 * 3. RADIAL BURST (Fala uderzeniowa / Eksplozja)
	 * Kulisty wybuch z ekranowaniem Line-of-Sight (magiczny granat, bomba ciśnieniowa).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Elements|Delivery", meta = (WorldContext = "WorldContextObject"))
	static void ApplyRadialBurst(
		const UObject* WorldContextObject,
		const FVector& Origin,
		float Radius,
		EStatusEffectType StatusType,
		float Duration,
		AActor* InstigatorActor = nullptr,
		float BaseDamage = 25.0f,
		float KnockbackForce = 1500.0f);

	/**
	 * 4. STATUS ZONE (Trwałe pole / Kałuża / Pożar)
	 * Spawnuje autonomiczny aktor strefy na podłodze, ścianie lub w powietrzu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Elements|Delivery", meta = (WorldContext = "WorldContextObject"))
	static AElementalStatusZone* SpawnStatusZone(
		const UObject* WorldContextObject,
		const FVector& Location,
		float Radius,
		EStatusEffectType StatusType,
		float Duration,
		EStatusZoneShapeMode ShapeMode = EStatusZoneShapeMode::SurfaceDisk,
		const FVector& SurfaceNormal = FVector(0.0f, 0.0f, 1.0f),
		AActor* InstigatorActor = nullptr);
};

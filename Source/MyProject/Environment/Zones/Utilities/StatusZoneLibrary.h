#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/HitResult.h"
#include "MyProject/Environment/Zones/Data/ZoneTypes.h"
#include "StatusZoneLibrary.generated.h"

class AStatusZoneBase;
class AVolumetricStatusZone;

/**
 * Zunifikowana biblioteka narzędziowa do tworzenia i wywoływania stref w świecie gry (Status Zone Engine).
 * Przeznaczona do użytku przez skille postaci, pułapki, czary, wybuchające propy i mechanizmy lochu.
 *
 * Oferuje 2 kluczowe archetypy strefowe:
 * 1. SpawnVolumetricZone: Trójwymiarowa strefa sferyczna wisząca w przestrzeni przez czas T (chmury, gazy, silence, slow).
 * 2. ApplyInstantBurst: Natychmiastowy wybuch z Line-of-Sight w klatce t0 bez tworzenia trwałego aktora strefy.
 */
UCLASS()
class MYPROJECT_API UStatusZoneLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 1. VOLUMETRIC ZONE (Przestrzenna strefa 3D)
	 * Tworzy pełną bryłę sferyczną AVolumetricStatusZone zawieszoną w powietrzu na określony czas (np. trujący gaz, dym, strefa uciszenia magii).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (WorldContext = "WorldContextObject"))
	static AVolumetricStatusZone* SpawnVolumetricZone(
		const UObject* WorldContextObject,
		const FVector& Location,
		float Radius,
		const FZoneEffectConfig& EffectConfig,
		float Duration,
		AActor* InstigatorActor = nullptr);

	/**
	 * 2. RADIAL BURST (Chwilowa fala uderzeniowa / wybuch 3D)
	 * Wykonuje test Line-of-Sight w promieniu Radius. Aplikuje obrażenia (InstantDamage), fizyczny odrzut (KnockbackForce)
	 * oraz status z EffectConfig do celów (postacie, propy) w zasięgu wzroku.
	 * Wykonuje wszechkierunkową projekcję 3D na otaczające powierzchnie lochu (posadzka, sufit, ściany) w UDungeonSurfaceSubsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (WorldContext = "WorldContextObject"))
	static void ApplyRadialBurst(
		const UObject* WorldContextObject,
		const FVector& Origin,
		float Radius,
		const FZoneEffectConfig& EffectConfig,
		float Duration = 4.0f,
		AActor* InstigatorActor = nullptr);

	/**
	 * Alias wsteczny dla ApplyRadialBurst (zachowanie pełnej kompatybilności wstecznej).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (WorldContext = "WorldContextObject"))
	static void ApplyInstantBurst(
		const UObject* WorldContextObject,
		const FVector& Origin,
		float Radius,
		const FZoneEffectConfig& EffectConfig,
		float Duration = 4.0f,
		AActor* InstigatorActor = nullptr);

	/**
	 * 3. POINT IMPACT (Uderzenie punktowe w pojedynczą powierzchnię lub cel)
	 * Dedykowane dla rzucanych butelek wody/oleju, koktajli Mołotowa, strzał żywiołowych, bełtów, pułapek naciskowych.
	 * Maluje wyłącznie uderzoną powierzchnię (od precyzyjnego 1-komórkowego trafienia przy SplashRadius <= 25cm do szerokiej plamy).
	 * Aplikuje obrażenia bezpośrednie i status celowi (postać, prop, struktura drewniana).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (WorldContext = "WorldContextObject"))
	static bool ApplyPointImpact(
		const UObject* WorldContextObject,
		const FHitResult& HitResult,
		float SplashRadius,
		EStatusEffectType StatusType,
		float Duration,
		float DirectDamage = 0.0f,
		AActor* InstigatorActor = nullptr);

	/**
	 * 4. POINT HIT (Uderzenie punktowe / bezpośrednie trafienie pociskiem - prosty wrapper)
	 * Trafienie pojedynczym pociskiem w cel (postać, strefa, niszczalna drewniana struktura).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (DefaultToSelf = "InstigatorActor"))
	static bool ApplyPointHit(
		AActor* TargetActor,
		const FVector& HitLocation,
		const FVector& HitNormal,
		EStatusEffectType StatusType,
		float Duration,
		float DirectDamage = 15.0f,
		float SplashRadius = 45.0f,
		AActor* InstigatorActor = nullptr);
};

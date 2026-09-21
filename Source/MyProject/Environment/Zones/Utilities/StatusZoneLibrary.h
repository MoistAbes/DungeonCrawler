#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/HitResult.h"
#include "MyProject/Environment/Zones/Data/ZoneTypes.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "MyProject/Environment/Zones/Shapes/VolumetricStatusZone.h"
#include "StatusZoneLibrary.generated.h"

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
	 * 3. INSTANT RADIAL BURST (Chwilowa fala uderzeniowa / wybuch)
	 * Wykonuje natychmiastowy test Line-of-Sight w promieniu Radius. Aplikuje obrażenia (InstantDamage), fizyczny odrzut (KnockbackForce)
	 * oraz status z EffectConfig do wszystkich celów w zasięgu wzroku. Nie tworzy trwałego aktora strefy (Zero-Alloc).
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
	 * 4. POINT HIT (Uderzenie punktowe / bezpośrednie trafienie pociskiem)
	 * Trafienie pojedynczym pociskiem w cel (postać, strefa, niszczalna drewniana struktura).
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (DefaultToSelf = "InstigatorActor"))
	static bool ApplyPointHit(
		AActor* TargetActor,
		const FVector& HitLocation,
		const FVector& HitNormal,
		EStatusEffectType StatusType,
		float Duration,
		AActor* InstigatorActor = nullptr);
};

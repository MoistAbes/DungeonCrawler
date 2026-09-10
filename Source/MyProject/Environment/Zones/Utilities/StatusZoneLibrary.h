#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/HitResult.h"
#include "MyProject/Environment/Zones/Data/ZoneTypes.h"
#include "MyProject/Environment/Zones/StatusZoneBase.h"
#include "MyProject/Environment/Zones/Shapes/SurfaceSplashZone.h"
#include "MyProject/Environment/Zones/Shapes/VolumetricStatusZone.h"
#include "StatusZoneLibrary.generated.h"

/**
 * Zunifikowana biblioteka narzędziowa do tworzenia i wywoływania stref w świecie gry (Status Zone Engine).
 * Przeznaczona do użytku przez skille postaci, pułapki, czary, wybuchające propy i mechanizmy lochu.
 *
 * Oferuje 3 kluczowe archetypy:
 * 1. ApplySurfaceSplash: Powłoka powierzchniowa (10-30 cm) na ścianie/podłodze podpięta pod trafiony obiekt (AttachToComponent).
 * 2. SpawnVolumetricZone: Trójwymiarowa strefa sferyczna wisząca w przestrzeni przez czas T (chmury, gazy, silence, slow).
 * 3. ApplyInstantBurst: Natychmiastowy wybuch z Line-of-Sight w klatce t0 bez tworzenia trwałego aktora strefy.
 */
UCLASS()
class MYPROJECT_API UStatusZoneLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Kwalifikuje, czy dany aktor może być podłożem pod strefę powierzchniową (Surface Splash).
	 * Akceptuje fundamenty lochu (ADungeonStructureBase) oraz geometrię poziomu (ABrush).
	 * Odrzuca dynamiczne rekwizyty (AInteractivePropBase) oraz postacie (APawn).
	 */
	static bool IsValidSurfaceTarget(const AActor* Actor);

	/**
	 * 1. SURFACE SPLASH (Płaska powłoka na ścianie, podłodze lub ruchomym mechanizmie)
	 * Spawnuje strefę ASurfaceSplashZone o grubości SurfaceHeight (domyślnie 25 cm) i przypina ją (AttachToComponent) do trafionego obiektu.
	 * Jeśli ściana ulegnie zburzeniu, strefa ulega automatycznemu zniszczeniu wraz z nią.
	 */
	UFUNCTION(BlueprintCallable, Category = "Environment|Zones|Delivery", meta = (WorldContext = "WorldContextObject"))
	static ASurfaceSplashZone* ApplySurfaceSplash(
		const UObject* WorldContextObject,
		const FHitResult& HitResult,
		float SplashRadius,
		float SurfaceHeight,
		const FZoneEffectConfig& EffectConfig,
		float Duration,
		AActor* InstigatorActor = nullptr);

	/**
	 * 2. VOLUMETRIC ZONE (Przestrzenna strefa 3D)
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

#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "ZoneTypes.generated.h"

/**
 * Kształt geometryczny i tryb przestrzenny strefy statusu w świecie gry.
 */
UENUM(BlueprintType)
enum class EZoneShapeType : uint8
{
	/** Cienka powłoka powierzchniowa (10-30 cm) przylegająca do geometrii ściany, podłogi lub mechanizmu */
	SurfaceSplash UMETA(DisplayName = "Surface Splash"),

	/** Przestrzenna, pełna bryła sferyczna w powietrzu (chmury, gazy, mgła, silence) */
	VolumetricSphere UMETA(DisplayName = "Volumetric Sphere")
};

/**
 * Zunifikowana konfiguracja parametrów i efektów strefy.
 * Pozwala na modularne definiowanie zarówno wybuchów jednorazowych, jak i trwałych stref w świecie.
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FZoneEffectConfig
{
	GENERATED_BODY()

	// --- Status Żywiołowy ---

	/** Nakładany status żywiołowy (np. Burning, Wet, Oiled). Nakładany przy wejściu i odświeżany co 1s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Zone|Status")
	EStatusEffectType AppliedStatus = EStatusEffectType::None;

	// --- Impuls Natychmiastowy / Wybuch (Apply Once on Burst / Hit) ---

	/** Jednorazowe obrażenia natychmiastowe przy wybuchu / detonacji */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Zone|Instant", meta = (ClampMin = "0.0"))
	float InstantDamage = 0.0f;

	/** Siła fizycznego impulsu odrzutu w klatce detonacji (Knockback Force) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Zone|Instant", meta = (ClampMin = "0.0"))
	float KnockbackForce = 0.0f;

	// --- Efekty Ciągłe (Over Time / While Inside) ---

	/** Ciągłe obrażenia zadawane co sekundę graczom i strukturom wewnątrz (np. ogień, kwas) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Zone|Continuous", meta = (ClampMin = "0.0"))
	float ContinuousDamagePerSec = 0.0f;
};

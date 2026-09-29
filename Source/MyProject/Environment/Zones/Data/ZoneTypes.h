#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "ZoneTypes.generated.h"

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

	/** Poziom (Tier) nakładanego statusu żywiołowego (0 = bazowy, 1 = zaawansowany, 2 = elitarny) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Zone|Status", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 StatusTier = 0;

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

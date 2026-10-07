#pragma once

#include "CoreMinimal.h"
#include "MovementModifierTypes.generated.h"

/**
 * Zunifikowany modyfikator parametrów motorycznych aktora (prędkość, tarcie, droga hamowania, immobilize).
 * Wykorzystywany jako modyfikator powierzchni (SurfaceModifier) oraz statusu ciała (BodyModifier).
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FMovementModifier
{
	GENERATED_BODY()

	/** Mnożnik maksymalnej prędkości poruszania się (1.0 = 100%, 0.85 = -15%, 1.20 = +20%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Movement", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SpeedMultiplier = 1.0f;

	/** Mnożnik przyczepności / sterowności na podłożu (1.0 = normalna, 0.05 = skrajny poślizg / drift) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Movement", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float GroundFrictionMultiplier = 1.0f;

	/** Mnożnik drogi hamowania po odpuszczeniu klawiszy WASD (1.0 = natychmiastowe stanięcie, 0.05 = daleki ślizg) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Movement", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float BrakingDecelerationMultiplier = 1.0f;

	/** Flaga całkowitego unieruchomienia postaci (Root / Paraliż / Sidła) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Movement")
	bool bImmobilized = false;

	/** Czy modyfikator zachowuje tożsamość bazową (nie wprowadza żadnych zmian) */
	bool IsIdentity() const
	{
		return !bImmobilized
			&& FMath::IsNearlyEqual(SpeedMultiplier, 1.0f, 0.001f)
			&& FMath::IsNearlyEqual(GroundFrictionMultiplier, 1.0f, 0.001f)
			&& FMath::IsNearlyEqual(BrakingDecelerationMultiplier, 1.0f, 0.001f);
	}

	/** Łączy ten modyfikator z innym (np. Surface + Body lub wiele efektów naraz) */
	FORCEINLINE void CombineWith(const FMovementModifier& Other)
	{
		SpeedMultiplier *= Other.SpeedMultiplier;
		GroundFrictionMultiplier = FMath::Min(GroundFrictionMultiplier, Other.GroundFrictionMultiplier);
		BrakingDecelerationMultiplier = FMath::Min(BrakingDecelerationMultiplier, Other.BrakingDecelerationMultiplier);
		bImmobilized = bImmobilized || Other.bImmobilized || (SpeedMultiplier <= 0.0f);
	}

	/** Zwraca nowy modyfikator będący połączeniem dwóch modyfikatorów */
	FORCEINLINE static FMovementModifier Combine(const FMovementModifier& A, const FMovementModifier& B)
	{
		FMovementModifier Result = A;
		Result.CombineWith(B);
		return Result;
	}

	bool operator==(const FMovementModifier& Other) const
	{
		return bImmobilized == Other.bImmobilized
			&& FMath::IsNearlyEqual(SpeedMultiplier, Other.SpeedMultiplier, 0.001f)
			&& FMath::IsNearlyEqual(GroundFrictionMultiplier, Other.GroundFrictionMultiplier, 0.001f)
			&& FMath::IsNearlyEqual(BrakingDecelerationMultiplier, Other.BrakingDecelerationMultiplier, 0.001f);
	}

	bool operator!=(const FMovementModifier& Other) const
	{
		return !(*this == Other);
	}
};

#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterialEnums.generated.h"

UENUM(BlueprintType)
enum class EPhysicalMaterialType : uint8
{
	Default  UMETA(DisplayName = "Default"),
	Glass    UMETA(DisplayName = "Glass"),
	Wood     UMETA(DisplayName = "Wood"),
	Stone    UMETA(DisplayName = "Stone"),
	Metal    UMETA(DisplayName = "Metal"),
	Flesh    UMETA(DisplayName = "Flesh")
};

/**
 * Fizyczne cechy tożsamości materiałowej.
 * Zamiast sztywnych list materiałów w statusach, żywioły odpytują cechy materiału (łatwopalność, przewodnictwo).
 */
USTRUCT(BlueprintType)
struct FPhysicalMaterialTraits
{
	GENERATED_BODY()

	/** Czy materiał jest z natury łatwopalny (np. Drewno, Ciało) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Material")
	bool bFlammable = false;

	/** Czy materiał z natury przewodzi prąd elektryczny (np. Metal, Ciało) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Material")
	bool bConductive = false;

	/** Czy materiał jest kruchy i łatwo pęka od uderzenia kinetycznego (np. Szkło) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Material")
	bool bFragile = false;
};

namespace PhysicalMaterialUtils
{
	FORCEINLINE FPhysicalMaterialTraits GetTraits(EPhysicalMaterialType Material)
	{
		FPhysicalMaterialTraits Traits;
		switch (Material)
		{
		case EPhysicalMaterialType::Wood:
			Traits.bFlammable = true;
			break;
		case EPhysicalMaterialType::Flesh:
			Traits.bFlammable = true;
			Traits.bConductive = true;
			break;
		case EPhysicalMaterialType::Metal:
			Traits.bConductive = true;
			break;
		case EPhysicalMaterialType::Glass:
			Traits.bFragile = true;
			break;
		case EPhysicalMaterialType::Stone:
			// Domyślnie brak cech specjalnych: niepalny izolator
			break;
		case EPhysicalMaterialType::Default:
		default:
			Traits.bFlammable = true;
			Traits.bConductive = true;
			break;
		}
		return Traits;
	}
}

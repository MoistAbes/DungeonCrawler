#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "PhysicalMaterialEnums.generated.h"

UENUM(BlueprintType)
enum class EPhysicalMaterialType : uint8
{
	Glass    UMETA(DisplayName = "Glass"),
	Wood     UMETA(DisplayName = "Wood"),
	Stone    UMETA(DisplayName = "Stone"),
	Metal    UMETA(DisplayName = "Metal"),
	Flesh    UMETA(DisplayName = "Flesh")
};

UENUM(BlueprintType)
enum class EDamageType : uint8
{
	Physical     UMETA(DisplayName = "Physical"),      // Cios mieczem, strzała, bezpośrednie uderzenie
	Kinetic      UMETA(DisplayName = "Kinetic"),       // Zderzenie ze ścianą, uderzenie głazem, siła kinetyczna
	Fire         UMETA(DisplayName = "Fire"),          // Ogień, płomienie, DoT od podpalenia
	Lightning    UMETA(DisplayName = "Lightning")      // Prąd, wyładowania elektryczne
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

	/** Czy materiał jest stałym paliwem podtrzymującym płomień i rozprzestrzeniającym go na sąsiadów */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Material")
	bool bSelfSustainingFuel = false;

	/** Czas (w sekundach) do kolejnej próby rozprzestrzenienia ognia na sąsiada z tego samego paliwa */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Material", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float FuelSpreadInterval = 2.0f;

	/** Obrażenia zadawane strukturze przez pojedynczy płonący kafel na sekundę */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Material", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float StructuralDamagePerSecond = 5.0f;

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
			Traits.bSelfSustainingFuel = true;
			Traits.FuelSpreadInterval = 2.0f;
			Traits.StructuralDamagePerSecond = 5.0f;
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
		default:
			// Domyślnie brak cech specjalnych: niepalny izolator
			break;
		}
		return Traits;
	}

	/** Mapuje status żywiołowy na odpowiedni typ obrażeń */
	FORCEINLINE EDamageType StatusToDamageType(EStatusEffectType Status)
	{
		switch (Status)
		{
		case EStatusEffectType::Burning:
			return EDamageType::Fire;
		case EStatusEffectType::Electrified:
			return EDamageType::Lightning;
		default:
			return EDamageType::Physical;
		}
	}

	/** Zwraca bazową odporność tożsamości materiałowej na dany typ obrażeń (1.0 = 100% odporności / immune, 0.0 = brak, ujemna = podatność) */
	FORCEINLINE float GetBaseResistance(EPhysicalMaterialType Material, EDamageType DamageType)
	{
		switch (Material)
		{
		case EPhysicalMaterialType::Stone:
			if (DamageType == EDamageType::Fire || DamageType == EDamageType::Lightning)
			{
				return 1.0f; // 100% odporności na ogień i prąd (kamień nie płonie i nie niszczy się od prądu)
			}
			return 0.0f;

		case EPhysicalMaterialType::Metal:
			if (DamageType == EDamageType::Fire || DamageType == EDamageType::Lightning)
			{
				return 1.0f; // 100% odporności na ogień i prąd (metal nie pali się ani nie niszczy od prądu)
			}
			if (DamageType == EDamageType::Kinetic)
			{
				return 0.5f; // 50% redukcji obrażeń kinetycznych (odporny na stłuczenie)
			}
			return 0.0f;

		case EPhysicalMaterialType::Wood:
			if (DamageType == EDamageType::Lightning)
			{
				return 0.5f; // 50% odporności na prąd (drewno jest częściowym izolatorem)
			}
			if (DamageType == EDamageType::Fire)
			{
				return 0.0f; // 0% odporności na ogień (drewno łatwo się pali i niszczy)
			}
			return 0.0f;

		case EPhysicalMaterialType::Glass:
			if (DamageType == EDamageType::Fire || DamageType == EDamageType::Lightning)
			{
				return 1.0f; // Szkło nie niszczy się od ognia ani prądu
			}
			if (DamageType == EDamageType::Kinetic)
			{
				return -0.5f; // Podatność (+50% obrażeń od kinetyki - kruche szkło łatwo pęka)
			}
			return 0.0f;

		case EPhysicalMaterialType::Flesh:
		default:
			return 0.0f; // Bazowe obrażenia bez redukcji
		}
	}
}

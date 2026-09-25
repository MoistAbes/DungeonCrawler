#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
#include "StatusEffectConfigAsset.generated.h"

/**
 * DataAsset zawierający kompletną konfigurację wszystkich statusów i ich tierów.
 * Edytowalny w edytorze Unreal Engine jako .uasset.
 */
UCLASS(BlueprintType)
class MYPROJECT_API UStatusEffectConfigAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Konfiguracje poszczególnych statusów (Burning, Wet, Oiled, Electrified) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Status")
	TMap<EStatusEffectType, FStatusEffectConfig> Statuses;

	/** Zwraca konfigurację danego statusu jeśli istnieje w tym assecie */
	const FStatusEffectConfig* FindConfig(EStatusEffectType Status) const
	{
		return Statuses.Find(Status);
	}
};

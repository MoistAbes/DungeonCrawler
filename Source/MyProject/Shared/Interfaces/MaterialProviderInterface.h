#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MaterialProviderInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UMaterialProviderInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interfejs dostarczający informację o tożsamości materiałowej obiektu (kamień, drewno, metal, ciało itp.).
 */
class MYPROJECT_API IMaterialProviderInterface
{
	GENERATED_BODY()

public:
	/** Zwraca typ materiału fizycznego obiektu */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Material")
	EPhysicalMaterialType GetMaterialType() const;
};

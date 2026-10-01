#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SurfaceGridTargetInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class USurfaceGridTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class USceneComponent;

/**
 * Kontrakt dla wszystkich aktorow w swiecie gry (fundamenty architektury lochu,
 * mechanizmy, plyty naciskowe, wrota, zapadnie), ktore moga byc stabilnym podlozem
 * pod komorki powierzchniowe (Sparse Surface Grid) w UDungeonSurfaceSubsystem.
 */
class MYPROJECT_API ISurfaceGridTargetInterface
{
	GENERATED_BODY()

public:
	/**
	 * Okresla, czy aktor moze w tej chwili przyjac nowe komorki powierzchniowe.
	 * Zwraca false np. w momencie zniszczenia struktury.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Custom|SurfaceGrid")
	bool CanReceiveSurfaceCells() const;

	/**
	 * Okresla, czy powierzchnia tego aktora jest dynamiczna/ruchoma (np. unoszaca sie krata, winda),
	 * czy statyczna (sciana, posadzka, plyta naciskowa).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Custom|SurfaceGrid")
	bool IsDynamicSurface() const;

	/**
	 * Zwraca komponent sceny, wzgledem ktorego transformowane sa komorki lokalne w przestrzeni dynamicznej.
	 * Domyslnie zwraca nullptr (co powoduje uzycie RootComponent aktora w subsystemie).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Custom|SurfaceGrid")
	USceneComponent* GetSurfaceTransformComponent() const;
	virtual USceneComponent* GetSurfaceTransformComponent_Implementation() const { return nullptr; }
};

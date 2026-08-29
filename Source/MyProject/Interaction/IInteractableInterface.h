#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IInteractableInterface.generated.h"

/**
 * Domenowy typ materiału (Java enum).
 * Odpowiada za reguły biznesowe niszczenia i reakcji na środowisko.
 */
UENUM(BlueprintType)
enum class EPropMaterialType : uint8
{
	Glass   UMETA(DisplayName = "Glass"),
	Wood    UMETA(DisplayName = "Wood"),
	Stone   UMETA(DisplayName = "Stone")
};

// Boilerplate silnikowy pod system refleksji (Class metadata)
UINTERFACE(MinimalAPI, BlueprintType)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Kontrakt domenowy interakcji.
 * Odpowiednik: public interface IInteractableService
 */
class MYPROJECT_API IInteractableInterface
{
	GENERATED_BODY()

public:
	/**
	 * Główna akcja interakcji (np. naciśnięcie klawisza E).
	 * @param Interactor Aktor inicjujący (odpowiednik Principal w Spring Security).
	 */
	virtual void Interact(AActor* Interactor) = 0;

	/**
	 * Warunek biznesowy: czy interakcja jest w tym momencie możliwa.
	 */
	virtual bool CanInteract(const AActor* Interactor) const = 0;

	/**
	 * Zdarzenie cyklu życia: obiekt został fizycznie pochwycony.
	 */
	virtual void OnGrabbed(AActor* Grabber) = 0;

	/**
	 * Zdarzenie cyklu życia: obiekt został upuszczony lub rzucony.
	 */
	virtual void OnDropped(AActor* Dropper) = 0;

	/**
	 * Pobiera masę obiektu w kg do walidacji udźwigu gracza.
	 */
	virtual float GetMass() const = 0;

	/**
	 * Zwraca domenowy typ materiału obiektu.
	 */
	virtual EPropMaterialType GetMaterialType() const = 0;
};
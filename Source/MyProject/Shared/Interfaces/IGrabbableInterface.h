#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IGrabbableInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UGrabbableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Odpowiednik: public interface GrabbableService
 * Służy do zarządzania fizyczną manipulacją obiektem przez noszenie/rzucanie (beczki, skrzynki, kamienie).
 */
class MYPROJECT_API IGrabbableInterface
{
	GENERATED_BODY()

public:
	/** Czy obiekt może zostać w tej chwili pochwycony */
	virtual bool CanGrab(const AActor* Grabber) const = 0;

	/** Hook cyklu życia: obiekt został złapany */
	virtual void OnGrabbed(AActor* Grabber) = 0;

	/** Hook cyklu życia: obiekt został upuszczony lub rzucony z zadaną prędkością */
	virtual void OnDropped(AActor* Dropper, const FVector& LaunchVelocity = FVector::ZeroVector) = 0;

	/** Pobiera masę obiektu w kg do walidacji udźwigu */
	virtual float GetMass() const = 0;

	/** Czy obiekt jest aktualnie niesiony przez postać */
	virtual bool IsGrabbed() const { return false; }
};

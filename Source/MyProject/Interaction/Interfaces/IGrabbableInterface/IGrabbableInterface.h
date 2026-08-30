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
 * Służy do zarządzania fizyczną manipulacją obiektem przez PhysicsHandle (wazy, skrzynki).
 */
class MYPROJECT_API IGrabbableInterface
{
	GENERATED_BODY()

public:
	/** Czy obiekt może zostać w tej chwili pochwycony */
	virtual bool CanGrab(const AActor* Grabber) const = 0;

	/** Hook cyklu życia: obiekt został złapany przez PhysicsHandle */
	virtual void OnGrabbed(AActor* Grabber) = 0;

	/** Hook cyklu życia: obiekt został upuszczony lub rzucony */
	virtual void OnDropped(AActor* Dropper) = 0;

	/** Pobiera masę obiektu w kg do walidacji udźwigu gracza */
	virtual float GetMass() const = 0;
	
};
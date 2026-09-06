#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MechanismReceiverInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UMechanismReceiverInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Kontrakt dla wszystkich obiektów w świecie gry, które mogą reagować
 * na sygnały aktywatorów (przełączników, płyt naciskowych, linek potykaczy, czujników itp.).
 */
class MYPROJECT_API IMechanismReceiverInterface
{
	GENERATED_BODY()

public:
	/**
	 * Zmienia stan mechanizmu (np. otwarcie/zamknięcie kraty, wysunięcie tłoka, zapalenie pochodni).
	 * BlueprintNativeEvent pozwala na implementację zarówno w C++, jak i w Blueprintach.
	 *
	 * @param bActive Czy aktywator przekazuje sygnał włączenia (true) czy wyłączenia (false)
	 * @param TriggeringActor Aktor odpowiedzialny za aktywację (np. gracz, rzucony głaz)
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Custom|Mechanism")
	void SetMechanismState(bool bActive, AActor* TriggeringActor);
};

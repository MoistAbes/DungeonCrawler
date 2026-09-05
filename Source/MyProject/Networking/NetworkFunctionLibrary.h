#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NetworkFunctionLibrary.generated.h"

/**
 * Biblioteka funkcji pomocniczych dla logiki sieciowej i multiplayer (1–6 graczy).
 * Izoluje powtarzalne sprawdzenia ról sieciowych, autorytetu serwera, optymalizację replikacji oraz formatowanie logów.
 */
UCLASS()
class MYPROJECT_API UNetworkFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Sprawdza, czy obiekt (Aktor lub Komponent) ma autorytet serwera (odporne na nullptr) */
    UFUNCTION(BlueprintPure, Category = "Custom|Networking", meta = (DefaultToSelf = "Context"))
    static bool HasAuthority(const UObject* Context);

    /** Sprawdza, czy obiekt wykonuje się na maszynie klienta (brak autorytetu) */
    UFUNCTION(BlueprintPure, Category = "Custom|Networking", meta = (DefaultToSelf = "Context"))
    static bool IsClient(const UObject* Context);

    /** Sprawdza, czy aktor jest lokalnie sterowaną postacią (Autonomous Proxy lub Standalone) */
    UFUNCTION(BlueprintPure, Category = "Custom|Networking")
    static bool IsLocallyControlled(const AActor* Actor);

    /** Zwraca czytelny prefiks sieciowy dla logów, np. [Server], [Client 1], [Client 2], [Standalone] */
    UFUNCTION(BlueprintPure, Category = "Custom|Networking", meta = (DefaultToSelf = "Context"))
    static FString GetNetRolePrefix(const UObject* Context);

    /** Konfiguruje optymalną kwantyzację kompresji różnicowej dla fizycznego aktora (oszczędność pasma) */
    UFUNCTION(BlueprintCallable, Category = "Custom|Networking")
    static void ConfigurePhysicsReplication(AActor* Actor);

    /** Bezpieczne podpięcie trzymanego obiektu pod uchwyt postaci z wyłączeniem fizyki Chaos na czas noszenia */
    UFUNCTION(BlueprintCallable, Category = "Custom|Networking")
    static void AttachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor);

    /** Bezpieczne odpięcie trzymanego obiektu z przywróceniem symulacji fizyki Chaos i nadaniem pędu */
    UFUNCTION(BlueprintCallable, Category = "Custom|Networking")
    static void DetachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor, const FVector& LaunchVelocity = FVector::ZeroVector);
};

namespace NetUtils
{
    /** Szybkie, inline'owe sprawdzenie autorytetu dla komponentu (sprawdza Ownera i HasAuthority) */
    FORCEINLINE bool HasAuthority(const UActorComponent* Component)
    {
        return Component && Component->GetOwner() && Component->GetOwner()->HasAuthority();
    }

    /** Szybkie, inline'owe sprawdzenie autorytetu dla Aktora */
    FORCEINLINE bool HasAuthority(const AActor* Actor)
    {
        return Actor && Actor->HasAuthority();
    }

    /** Zwraca prefiks [Server] lub [Client X] dla danego komponentu/aktora do logów */
    FORCEINLINE FString GetNetRolePrefix(const UObject* Context)
    {
        return UNetworkFunctionLibrary::GetNetRolePrefix(Context);
    }

    /** Konfiguruje kwantyzację pozycji i rotacji dla FRepMovement w AActorze */
    FORCEINLINE void SetupQuantizedPhysicsReplication(AActor* Actor)
    {
        UNetworkFunctionLibrary::ConfigurePhysicsReplication(Actor);
    }

    /** Replikowane podpięcie niesionego propa (Attach-on-Grab) */
    FORCEINLINE void AttachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor)
    {
        UNetworkFunctionLibrary::AttachCarriedProp(PropActor, PropMesh, CarrierActor);
    }

    /** Replikowane odpięcie i rzucenie propem (Detach-and-Throw) */
    FORCEINLINE void DetachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor, const FVector& LaunchVelocity = FVector::ZeroVector)
    {
        UNetworkFunctionLibrary::DetachCarriedProp(PropActor, PropMesh, CarrierActor, LaunchVelocity);
    }
}

/**
 * Deklaratywne makra strażnicze (Spring-style Guard Macros).
 * Zabezpieczają wejście do metody autorytatywnej na samym jej początku:
 * REQUIRE_AUTHORITY();
 */
#define REQUIRE_AUTHORITY() \
    if (!NetUtils::HasAuthority(this)) return;

#define REQUIRE_AUTHORITY_RET(ReturnValue) \
    if (!NetUtils::HasAuthority(this)) return (ReturnValue);

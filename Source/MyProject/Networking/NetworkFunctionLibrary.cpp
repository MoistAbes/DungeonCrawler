#include "NetworkFunctionLibrary.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"

bool UNetworkFunctionLibrary::HasAuthority(const UObject* Context)
{
    if (!Context)
    {
        return false;
    }

    if (const AActor* Actor = Cast<AActor>(Context))
    {
        return Actor->HasAuthority();
    }

    if (const UActorComponent* Component = Cast<UActorComponent>(Context))
    {
        return Component->GetOwner() && Component->GetOwner()->HasAuthority();
    }

    return false;
}

bool UNetworkFunctionLibrary::IsClient(const UObject* Context)
{
    return !HasAuthority(Context);
}

bool UNetworkFunctionLibrary::IsLocallyControlled(const AActor* Actor)
{
    if (!Actor)
    {
        return false;
    }

    if (const APawn* Pawn = Cast<APawn>(Actor))
    {
        return Pawn->IsLocallyControlled();
    }

    return false;
}

FString UNetworkFunctionLibrary::GetNetRolePrefix(const UObject* Context)
{
    if (!Context)
    {
        return TEXT("[NullContext]");
    }

    const AActor* Actor = Cast<AActor>(Context);
    if (!Actor)
    {
        if (const UActorComponent* Comp = Cast<UActorComponent>(Context))
        {
            Actor = Comp->GetOwner();
        }
    }

    if (!Actor)
    {
        return TEXT("[UnknownNetRole]");
    }

    const ENetMode NetMode = Actor->GetNetMode();
    if (NetMode == NM_Standalone)
    {
        return TEXT("[Standalone]");
    }

    if (Actor->HasAuthority())
    {
        return TEXT("[Server]");
    }

    if (const APawn* Pawn = Cast<APawn>(Actor))
    {
        if (Pawn->IsLocallyControlled())
        {
            return TEXT("[Client]");
        }
    }

    return TEXT("[Client (Remote)]");
}

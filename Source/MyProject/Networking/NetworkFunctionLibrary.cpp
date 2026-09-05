#include "NetworkFunctionLibrary.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MyProject/Player/PlayerCharacter.h"
#include "MyProject/Shared/Components/InteractionComponent/InteractionComponent.h"

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
    if (!Context)
    {
        return false;
    }

    const UWorld* World = Context->GetWorld();
    if (!World)
    {
        return false;
    }

    const ENetMode NetMode = World->GetNetMode();
    return (NetMode == NM_Client);
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

    return Actor->HasAuthority();
}

FString UNetworkFunctionLibrary::GetNetRolePrefix(const UObject* Context)
{
    if (!Context)
    {
        return TEXT("[Unknown]");
    }

    const UWorld* World = Context->GetWorld();
    if (!World)
    {
        return TEXT("[NoWorld]");
    }

    const ENetMode NetMode = World->GetNetMode();
    if (NetMode == NM_Standalone)
    {
        return TEXT("[Standalone]");
    }

    if (HasAuthority(Context))
    {
        return TEXT("[Server]");
    }

    return TEXT("[Client]");
}

void UNetworkFunctionLibrary::ConfigurePhysicsReplication(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    // 1. Włączamy podstawową replikację aktora i ruchu przez publiczne metody AActor
    Actor->SetReplicates(true);
    Actor->SetReplicateMovement(true);

    // 2. Optymalizacja pasma (Kwantyzacja kompresji różnicowej)
    FRepMovement RepMove = Actor->GetReplicatedMovement();
    RepMove.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
    RepMove.VelocityQuantizationLevel = EVectorQuantization::RoundWholeNumber;
    RepMove.RotationQuantizationLevel = ERotatorQuantization::ByteComponents;
    Actor->SetReplicatedMovement(RepMove);

    // 3. Częstotliwość aktualizacji fizyki: 30-45 Hz wystarcza w zupełności dla propów w lochu
    Actor->SetNetUpdateFrequency(30.0f);
    Actor->SetMinNetUpdateFrequency(5.0f);
}

void UNetworkFunctionLibrary::AttachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor)
{
    if (!PropActor || !CarrierActor)
    {
        return;
    }

    // 1. Odpinamy od wszelkich rodziców w świecie.
    // Prop nie jest przyczepiany "na sztywno" (AttachToComponent), lecz prowadzony kinematycznie
    // ze sweepem w InteractionComponent. Zapobiega to efektowi nieskończenie silnego spychacza.
    FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
    PropActor->DetachFromActor(DetachRules);

    // 2. Wyłączamy symulację fizyki dynamicznej
    if (PropMesh)
    {
        PropMesh->SetSimulatePhysics(false);

        // Ignorujemy kolizję z niosącym graczem, aby nie blokować własnej kapsuły
        PropMesh->IgnoreActorWhenMoving(CarrierActor, true);

        // Zachowujemy pełną blokadę pocisków, magii i świata zewnętrznego (funkcja tarczy)
        PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        PropMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
        PropMesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        PropMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    }

    // 3. Zarządzanie siecią: lokalny klient wyłącza przychodzącą replikację ruchu na czas chwytu,
    // aby pakiety z serwera nie walczyły z lokalną, 140+ FPS predykcją sweepa
    if (UNetworkFunctionLibrary::IsLocallyControlled(CarrierActor) && !CarrierActor->HasAuthority())
    {
        PropActor->SetReplicateMovement(false);
    }
    else
    {
        PropActor->SetReplicateMovement(true);
        PropActor->SetNetUpdateFrequency(60.0f);
    }

    // 4. Włączamy On-Demand Tick komponentu interakcji na postaci niosącej propa
    if (UInteractionComponent* InterComp = CarrierActor->FindComponentByClass<UInteractionComponent>())
    {
        InterComp->NotifyCarriedPropAttached(PropActor);
    }
}

void UNetworkFunctionLibrary::DetachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor, const FVector& LaunchVelocity)
{
    if (!PropActor)
    {
        return;
    }

    // 1. Upewniamy się, że obiekt nie ma żadnych podpięć
    FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
    PropActor->DetachFromActor(DetachRules);

    // 2. Przywracamy kolizję z postacią niosącą
    if (PropMesh && CarrierActor)
    {
        PropMesh->IgnoreActorWhenMoving(CarrierActor, false);
    }

    // 3. Przywracamy domyślną częstotliwość replikacji i replikację ruchu na wszystkich maszynach
    PropActor->SetNetUpdateFrequency(30.0f);
    PropActor->SetReplicateMovement(true);

    // 4. Przywracamy symulację fizyki Chaos
    if (PropMesh)
    {
        PropMesh->SetSimulatePhysics(true);
        PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        PropMesh->SetNotifyRigidBodyCollision(true);
        PropMesh->WakeRigidBody();

        // 5. Aplikujemy prędkość początkową rzutu / pędu zamachu (na Serwerze)
        if (!LaunchVelocity.IsNearlyZero())
        {
            PropMesh->SetPhysicsLinearVelocity(LaunchVelocity);
        }
    }

    // 6. Powiadamiamy InteractionComponent postaci o zakończeniu niesienia
    if (CarrierActor)
    {
        if (UInteractionComponent* InterComp = CarrierActor->FindComponentByClass<UInteractionComponent>())
        {
            InterComp->NotifyCarriedPropDetached();
        }
    }

    // 7. Wymuszamy natychmiastowe rozesłanie paczki fizyki z serwera do wszystkich klientów
    if (PropActor->HasAuthority())
    {
        PropActor->ForceNetUpdate();
    }
}

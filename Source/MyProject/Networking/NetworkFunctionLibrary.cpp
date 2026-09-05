#include "NetworkFunctionLibrary.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

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

    // 1. Wyłączamy symulację fizyki i kolizję z postacią
    if (PropMesh)
    {
        PropMesh->SetSimulatePhysics(false);
        PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    }

    // 2. Pozycjonujemy obiekt bezpośrednio przed kapsułą postaci (X = +100 cm przed brzuchem, Z = +15 cm wysokość klatki)
    const FVector OffsetLocation(100.0f, 0.0f, 15.0f);
    const FRotator OffsetRotation(0.0f, 0.0f, 0.0f);

    USceneComponent* AttachParent = CarrierActor->GetRootComponent();
    if (const ACharacter* Character = Cast<ACharacter>(CarrierActor))
    {
        if (Character->GetCapsuleComponent())
        {
            AttachParent = Character->GetCapsuleComponent();
        }
    }

    if (AttachParent)
    {
        FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
        PropActor->AttachToComponent(AttachParent, AttachRules);
        PropActor->SetActorRelativeLocation(OffsetLocation);
        PropActor->SetActorRelativeRotation(OffsetRotation);
    }
}

void UNetworkFunctionLibrary::DetachCarriedProp(AActor* PropActor, UPrimitiveComponent* PropMesh, AActor* CarrierActor, const FVector& LaunchVelocity)
{
    if (!PropActor)
    {
        return;
    }

    // 1. Odpinamy od postaci w świecie
    FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
    PropActor->DetachFromActor(DetachRules);

    // 2. Jawnie przywracamy replikację ruchu
    PropActor->SetReplicateMovement(true);

    // 3. Przywracamy symulację fizyki Chaos
    if (PropMesh)
    {
        PropMesh->SetSimulatePhysics(true);
        PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        PropMesh->SetNotifyRigidBodyCollision(true);
        PropMesh->WakeRigidBody();

        // 4. Aplikujemy ewentualny impuls rzutu (na Serwerze)
        if (!LaunchVelocity.IsNearlyZero())
        {
            PropMesh->AddImpulse(LaunchVelocity, NAME_None, true);
        }
    }

    // 5. Wymuszamy natychmiastowe rozesłanie paczki fizyki z serwera do wszystkich klientów
    if (PropActor->HasAuthority())
    {
        PropActor->ForceNetUpdate();
    }
}

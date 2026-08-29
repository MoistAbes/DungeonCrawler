#include "../InteractionComponent/InteractionComponent.h"
#include "../IInteractableInterface.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "GameFramework/Character.h"
#include "DrawDebugHelpers.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        PhysicsHandle = Owner->FindComponentByClass<UPhysicsHandleComponent>();
        if (!PhysicsHandle)
        {
            UE_LOG(LogTemp, Error, TEXT("[InteractionService] PhysicsHandleComponent NOT FOUND on Actor: %s!"), *Owner->GetName());
        }
    }
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (GrabbedActor && PhysicsHandle)
    {
        UpdateHoldLocation();
    }
}

void UInteractionComponent::PrimaryInteract()
{
    UE_LOG(LogTemp, Warning, TEXT("[InteractionService] PrimaryInteract triggered."));

    if (GrabbedActor)
    {
        UE_LOG(LogTemp, Log, TEXT("[InteractionService] Releasing currently grabbed actor: %s"), *GrabbedActor->GetName());
        ReleaseProp();
        return;
    }

    FHitResult HitResult;
    if (!PerformTrace(HitResult))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Raycast Trace missed (No Actor hit)."));
        return;
    }

    AActor* HitActor = HitResult.GetActor();
    UE_LOG(LogTemp, Log, TEXT("[InteractionService] Trace Hit Actor: %s"), *HitActor->GetName());

    if (!HitActor->Implements<UInteractableInterface>())
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Actor %s does NOT implement IInteractableInterface."), *HitActor->GetName());
        return;
    }

    IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor);
    if (!Interactable || !Interactable->CanInteract(GetOwner()))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService] CanInteract returned FALSE."));
        return;
    }

    if (Interactable->GetMass() > MaxCarryMass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Prop mass (%.1f kg) exceeds MaxCarryMass (%.1f kg)."), Interactable->GetMass(), MaxCarryMass);
        Interactable->Interact(GetOwner());
        return;
    }

    GrabProp(HitActor, HitResult.GetComponent());
}

bool UInteractionComponent::PerformTrace(FHitResult& OutHit) const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return false;

    // Pobieramy rotację kamery (kierunek patrzenia gracza)
    FVector CameraLocation;
    FRotator CameraRotation;
    Owner->GetActorEyesViewPoint(CameraLocation, CameraRotation);

    // Punkt startowy: środek postaci (klatka piersiowa), a nie odległa kamera TPP
    const FVector TraceStart = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
    const FVector TraceEnd = TraceStart + (CameraRotation.Vector() * TraceDistance);

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Owner);  

    const bool bHit = GetWorld()->LineTraceSingleByChannel(
        OutHit,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        Params
    );

    // Wizualizacja: zielony = trafienie w obiekt, czerwony = brak trafienia
    DrawDebugLine(GetWorld(), TraceStart, TraceEnd, bHit ? FColor::Green : FColor::Red, false, 2.0f, 0, 2.0f);

    return bHit;
}

void UInteractionComponent::GrabProp(AActor* TargetActor, UPrimitiveComponent* ComponentToGrab)
{
    if (!TargetActor || !ComponentToGrab || !PhysicsHandle) return;

    GrabbedActor = TargetActor;
    GrabbedComponent = ComponentToGrab;

    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(GrabbedActor))
    {
        Interactable->OnGrabbed(GetOwner());
    }

    // Wyłączamy kolizję z graczem na czas niesienia
    if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
    {
        GrabbedComponent->IgnoreActorWhenMoving(CharOwner, true);
    }

    // Wymuszenie wybudzenia i rejestracji uderzeń
    GrabbedComponent->SetNotifyRigidBodyCollision(true);
    GrabbedComponent->WakeRigidBody();

    PhysicsHandle->GrabComponentAtLocationWithRotation(
        GrabbedComponent,
        NAME_None,
        GrabbedComponent->GetComponentLocation(),
        GrabbedComponent->GetComponentRotation()
    );

    SetComponentTickEnabled(true);
    UE_LOG(LogTemp, Log, TEXT("[InteractionService] Successfully grabbed: %s"), *GrabbedActor->GetName());
}

void UInteractionComponent::ReleaseProp()
{
    if (!GrabbedActor || !PhysicsHandle) return;

    SetComponentTickEnabled(false);
    PhysicsHandle->ReleaseComponent();

    // Przywrócenie pełnych właściwości fizycznych
    if (GrabbedComponent)
    {
        GrabbedComponent->SetNotifyRigidBodyCollision(true);
        GrabbedComponent->WakeRigidBody();

        if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
        {
            GrabbedComponent->IgnoreActorWhenMoving(CharOwner, false);
        }
    }

    if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(GrabbedActor))
    {
        Interactable->OnDropped(GetOwner());
    }

    GrabbedActor = nullptr;
    GrabbedComponent = nullptr;
}

void UInteractionComponent::ThrowCurrentProp()
{
    if (!GrabbedActor || !GrabbedComponent) return;

    UPrimitiveComponent* PropPhysicsComp = GrabbedComponent;
    AActor* Owner = GetOwner();

    // 1. Uwalniamy uchwyt
    ReleaseProp();

    // 2. Aplikujemy wektor prędkości rzutu
    if (PropPhysicsComp && Owner)
    {
        FVector ViewLoc;
        FRotator ViewRot;
        Owner->GetActorEyesViewPoint(ViewLoc, ViewRot);

        PropPhysicsComp->SetNotifyRigidBodyCollision(true);
        PropPhysicsComp->WakeRigidBody();

        // Stała prędkość wyrzutu w kierunku kamery (bVelChange = true)
        const FVector LaunchVelocity = ViewRot.Vector() * ThrowImpulseStrength;
        PropPhysicsComp->AddImpulse(LaunchVelocity, NAME_None, true);

        UE_LOG(LogTemp, Warning, TEXT("[InteractionService] Prop thrown with LaunchVelocity: %s"), *LaunchVelocity.ToString());
    }
}

void UInteractionComponent::UpdateHoldLocation()
{
    const AActor* Owner = GetOwner();
    if (!Owner || !PhysicsHandle) return;

    FVector CameraLocation;
    FRotator CameraRotation;
    Owner->GetActorEyesViewPoint(CameraLocation, CameraRotation);

    const FVector TargetLocation = CameraLocation + (CameraRotation.Vector() * HoldDistance);
    PhysicsHandle->SetTargetLocationAndRotation(TargetLocation, CameraRotation);
}
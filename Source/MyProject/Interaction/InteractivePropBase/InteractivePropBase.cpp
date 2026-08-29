#include "InteractivePropBase.h"
#include "Components/StaticMeshComponent.h"

AInteractivePropBase::AInteractivePropBase()
{
    PrimaryActorTick.bCanEverTick = false;

    // Inicjalizacja komponentu (Wstrzyknięcie zależności w konstruktorze)
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    RootComponent = MeshComponent;

    // Domyślna konfiguracja fizyki i publikacji zdarzeń
    MeshComponent->SetSimulatePhysics(true);
    MeshComponent->SetNotifyRigidBodyCollision(true);
    MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
    MeshComponent->SetGenerateOverlapEvents(false);
}

void AInteractivePropBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    if (MeshComponent)
    {
        // Rejestracja brokera zdarzeń (EventListener)
        MeshComponent->OnComponentHit.RemoveAll(this);
        MeshComponent->OnComponentHit.AddDynamic(this, &AInteractivePropBase::HandleImpactDamage);
    }
}

void AInteractivePropBase::BeginPlay()
{
    Super::BeginPlay();

    CurrentDurability = Durability;

    if (MeshComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PropLifecycle] %s INITIALIZED | Durability: %.1f | Threshold: %.1f"), 
            *GetName(), CurrentDurability, DamageImpactThreshold);

        // Leniwa fizyka: uśpienie obiektu do momentu interakcji
        MeshComponent->PutRigidBodyToSleep();
    }
}

void AInteractivePropBase::Interact(AActor* Interactor)
{
    if (MeshComponent)
    {
        MeshComponent->WakeRigidBody();
    }
}

bool AInteractivePropBase::CanInteract(const AActor* Interactor) const
{
    return bCanBeGrabbed;
}

void AInteractivePropBase::OnGrabbed(AActor* Grabber)
{
    if (MeshComponent)
    {
        MeshComponent->WakeRigidBody();
    }
}

void AInteractivePropBase::OnDropped(AActor* Dropper)
{
    if (MeshComponent)
    {
        MeshComponent->WakeRigidBody();
    }
}

float AInteractivePropBase::GetMass() const
{
    return MeshComponent ? MeshComponent->GetMass() : 0.0f;
}

void AInteractivePropBase::HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                             UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                             const FHitResult& Hit)
{
    const FString OtherActorName = OtherActor ? OtherActor->GetName() : TEXT("StaticGeometry/World");
    const float ImpactForce = NormalImpulse.Size();

    // Log każdego zarejestrowanego kontaktu fizycznego
    UE_LOG(LogTemp, Warning, TEXT("[PropImpact RAW] %s HIT -> %s | Force: %.1f | Threshold: %.1f"),
        *GetName(), *OtherActorName, ImpactForce, DamageImpactThreshold);

    if (MaterialType == EPropMaterialType::Stone)
    {
        return;
    }

    if (ImpactForce < DamageImpactThreshold)
    {
        return;
    }

    if (MaterialType == EPropMaterialType::Glass)
    {
        UE_LOG(LogTemp, Error, TEXT("[PropImpact] >>> GLASS SHATTERED <<< : %s"), *GetName());
        BreakProp();
        return;
    }

    if (MaterialType == EPropMaterialType::Wood)
    {
        const float Damage = (ImpactForce - DamageImpactThreshold) * 0.001f;
        CurrentDurability -= Damage;
        UE_LOG(LogTemp, Warning, TEXT("[PropImpact] Wood Damaged: -%.1f HP (Remaining: %.1f)"), Damage, CurrentDurability);

        if (CurrentDurability <= 0.0f)
        {
            BreakProp();
        }
    }
}

void AInteractivePropBase::BreakProp()
{
    Destroy();
}
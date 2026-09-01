#include "InteractivePropBase.h"
#include "Components/StaticMeshComponent.h"
#include "MyProject/MyProject.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"

AInteractivePropBase::AInteractivePropBase()
{
    PrimaryActorTick.bCanEverTick = false;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    RootComponent = MeshComponent;

    // 1. Symulacja sztywnej bryły Chaos z ciągłą detekcją kolizji (CCD)
    MeshComponent->SetSimulatePhysics(true);
    MeshComponent->SetNotifyRigidBodyCollision(true);
    MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
    MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
    MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
    MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    MeshComponent->SetUseCCD(true);

    // 2. Kontrola wchodzenia i stania postaci na propie
    MeshComponent->CanCharacterStepUpOn = ECB_Yes;

    // 3. Tłumienie kątowe i liniowe stabilizujące fizykę brył
    MeshComponent->SetLinearDamping(0.8f);
    MeshComponent->SetAngularDamping(5.0f);

    DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));
}

void AInteractivePropBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    if (MeshComponent)
    {
        MeshComponent->OnComponentHit.RemoveAll(this);
        MeshComponent->OnComponentHit.AddDynamic(this, &AInteractivePropBase::HandleImpactDamage);
    }

    if (DamageableComponent)
    {
        DamageableComponent->OnDestroyed.RemoveAll(this);
        DamageableComponent->OnDestroyed.AddDynamic(this, &AInteractivePropBase::HandleOnDestroyed);
    }
}

void AInteractivePropBase::BeginPlay()
{
    Super::BeginPlay();

    if (MeshComponent)
    {
        MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
        MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
        MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
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
    return true;
}

bool AInteractivePropBase::CanGrab(const AActor* Grabber) const
{
    const bool bIsAlive = DamageableComponent ? !DamageableComponent->IsDestroyed() : true;
    return bCanBeGrabbed && bIsAlive;
}

void AInteractivePropBase::OnGrabbed(AActor* Grabber)
{
    bIsBeingCarried = true;
    if (MeshComponent)
    {
        MeshComponent->WakeRigidBody();
    }
}

void AInteractivePropBase::OnDropped(AActor* Dropper)
{
    bIsBeingCarried = false;
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
    if (!MeshComponent || bIsBeingCarried) return;

    // 1. Parametry fizyczne propa
    const FVector PropVelocity = MeshComponent->GetPhysicsLinearVelocity();

    // 2. Parametry obiektu uderzającego
    FVector OtherVelocity = FVector::ZeroVector;
    if (OtherComp && OtherComp->IsSimulatingPhysics())
    {
        OtherVelocity = OtherComp->GetPhysicsLinearVelocity();
    }
    else if (OtherActor)
    {
        OtherVelocity = OtherActor->GetVelocity();
    }

    // 3. Względna prędkość w osi normalnej zderzenia (uderzenie prostopadłe)
    const FVector RelativeVelocity = PropVelocity - OtherVelocity;
    const float ImpactSpeed = -FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal);

    // 4. Aplikacja obrażeń kinetycznych (tylko gdy zbliżają się do siebie z dużą siłą)
    if (ImpactSpeed > 0.0f && DamageableComponent)
    {
        DamageableComponent->ApplyKineticImpact(ImpactSpeed);
    }
}

void AInteractivePropBase::HandleOnDestroyed(AActor* DestroyedActor)
{
    UE_LOG(LogTemp, Error, TEXT("[PropEntity] Object destroyed via Event: %s"), *GetName());
    Destroy();
}

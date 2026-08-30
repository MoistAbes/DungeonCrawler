#include "InteractivePropBase.h"
#include "Components/StaticMeshComponent.h"
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
    if (!MeshComponent) return;

    // 1. Parametry fizyczne propa
    const float PropMass = MeshComponent->GetMass();
    const FVector PropVelocity = MeshComponent->GetPhysicsLinearVelocity();
    const FVector PropAngVel = MeshComponent->GetPhysicsAngularVelocityInDegrees();
    const FVector CenterOfMass = MeshComponent->GetCenterOfMass();
    const FVector LeverArm = Hit.ImpactPoint - CenterOfMass;

    // 2. Parametry obiektu uderzającego
    FVector OtherVelocity = FVector::ZeroVector;
    float OtherMass = 0.0f;
    const FString OtherName = OtherActor ? OtherActor->GetName() : TEXT("World/None");

    if (OtherComp && OtherComp->IsSimulatingPhysics())
    {
        OtherVelocity = OtherComp->GetPhysicsLinearVelocity();
        OtherMass = OtherComp->GetMass();
    }
    else if (OtherActor)
    {
        OtherVelocity = OtherActor->GetVelocity();
    }

    // 3. Względna prędkość w osi zderzenia
    const FVector RelativeVelocity = PropVelocity - OtherVelocity;
    const float ImpactSpeed = FMath::Abs(FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal));
    
    //DEBUG STUFF
    // const float ImpulseMag = NormalImpulse.Size();

    // Szacowany moment siły (Torque): r x F
    // const FVector EstimatedTorque = FVector::CrossProduct(LeverArm, NormalImpulse);

    // UE_LOG(LogTemp, Warning, TEXT("[PHYS_DIAG|PROP_HIT] %s (Mass: %.1f kg) HIT BY %s (Mass: %.1f kg) | ImpSpeed: %.1f cm/s | NormalImpulse: %.1f (Vec: %s) | PropLinVel: %.1f | PropAngVel: %.1f deg/s | LeverArm: (%.1f, %.1f, %.1f) | TorqueMag: %.1f | ImpactNormal: %s"),
    //     *GetName(), PropMass, *OtherName, OtherMass, ImpactSpeed, ImpulseMag, *NormalImpulse.ToString(), PropVelocity.Size(), PropAngVel.Size(), LeverArm.X, LeverArm.Y, LeverArm.Z, EstimatedTorque.Size(), *Hit.ImpactNormal.ToString());

    // 4. Aplikacja obrażeń kinetycznych
    if (DamageableComponent)
    {
        DamageableComponent->ApplyKineticImpact(ImpactSpeed);
    }
}

void AInteractivePropBase::HandleOnDestroyed(AActor* DestroyedActor)
{
    UE_LOG(LogTemp, Error, TEXT("[PropEntity] Object destroyed via Event: %s"), *GetName());
    Destroy();
}

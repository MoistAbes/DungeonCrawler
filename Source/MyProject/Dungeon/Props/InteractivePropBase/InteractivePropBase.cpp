#include "InteractivePropBase.h"

#include "Components/StaticMeshComponent.h"
#include "MyProject/Environment/Kinetic/Components/KnockbackComponent/KnockbackComponent.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"

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
    StatusEffectComponent = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("StatusEffectComponent"));
}

void AInteractivePropBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    if (MeshComponent)
    {
        MeshComponent->OnComponentHit.AddDynamic(this, &AInteractivePropBase::HandleImpactDamage);
    }

    if (DamageableComponent)
    {
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
    if (!MeshComponent || bIsBeingCarried || !OtherActor || OtherActor == this)
    {
        return;
    }

    // 1. Jeśli obiekt uderzający jest aktualnie trzymany przez postać - ignorujemy ocieranie
    if (const IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(OtherActor))
    {
        if (Grabbable->IsGrabbed())
        {
            return;
        }
    }

    // 2. Obliczamy prostopadłą prędkość zderzenia przez zunifikowaną bibliotekę kinetyczną
    const float ImpactSpeed = UKineticForceLibrary::CalculateImpactSpeed(MeshComponent, OtherActor, OtherComp, Hit.ImpactNormal);

    // 3. Aplikacja obrażeń kinetycznych na samego siebie
    if (ImpactSpeed > 0.0f && DamageableComponent)
    {
        DamageableComponent->ApplyKineticImpact(ImpactSpeed);
    }

    // 4. Przekazywanie pędu i odrzutu uderzanemu celowi (np. gracz lub potwory z KnockbackComponent)
    if (bTransferKineticKnockback && OtherActor)
    {
        const FVector PropVelocity = MeshComponent->GetPhysicsLinearVelocity();
        const float PropSpeed = PropVelocity.Size();

        if (PropSpeed >= MinImpactSpeedForKnockback)
        {
            // Sprawdzamy prędkość, z jaką prop nadlatywał w stronę celu
            const FVector ToTarget = (OtherActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
            const float ClosingSpeedByNormal = -FVector::DotProduct(PropVelocity, Hit.ImpactNormal);
            const float ClosingSpeedByDirection = FVector::DotProduct(PropVelocity, ToTarget);
            const float EffectivePropSpeed = FMath::Max(ClosingSpeedByNormal, ClosingSpeedByDirection);

            if (EffectivePropSpeed >= MinImpactSpeedForKnockback)
            {
                // Skalowanie masą (np. 170kg uderza mocniej niż lekki stołek 25kg)
                const float Mass = GetMass();
                const float MassFactor = FMath::Clamp(Mass > 0.0f ? (Mass / 50.0f) : 1.0f, 0.5f, 3.5f);
                const float KnockbackForce = EffectivePropSpeed * MassFactor * KnockbackStrengthMultiplier;

                // Kierunek odrzutu zgodny z wektorem lotu propa, z lekkim uniesieniem w górę (Upward Bias)
                FVector KnockbackDir = PropVelocity.GetSafeNormal();
                if (KnockbackDir.IsNearlyZero())
                {
                    KnockbackDir = ToTarget;
                }
                KnockbackDir.Z = FMath::Clamp(KnockbackDir.Z + 0.25f, 0.1f, 1.0f);
                KnockbackDir.Normalize();

                // Aplikujemy odrzut na cel
                if (UKnockbackComponent* TargetKnockback = OtherActor->FindComponentByClass<UKnockbackComponent>())
                {
                    TargetKnockback->ApplyImpulseForce(KnockbackDir, KnockbackForce, this);
                }

                // Zadajemy obrażenia uderzonemu celowi
                if (UDamageableComponent* TargetDamageable = OtherActor->FindComponentByClass<UDamageableComponent>())
                {
                    TargetDamageable->ApplyKineticImpact(EffectivePropSpeed * MassFactor);
                }

                UE_LOG(LogTemp, Warning, TEXT("[PropKineticTransfer] %s slammed into %s at Speed: %.1f cm/s | Force: %.1f (Mass: %.1f kg)"),
                    *GetName(), *OtherActor->GetName(), EffectivePropSpeed, KnockbackForce, Mass);
            }
        }
    }
}

void AInteractivePropBase::HandleOnDestroyed(AActor* DestroyedActor)
{
    UE_LOG(LogTemp, Error, TEXT("[PropEntity] Object destroyed via Event: %s"), *GetName());
    Destroy();
}

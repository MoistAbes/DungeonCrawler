#include "InteractivePropBase.h"

#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Components/KnockbackComponent/KnockbackComponent.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"

AInteractivePropBase::AInteractivePropBase()
{
    // Domyślnie Tick jest wyłączony (0% CPU dla leżących propów w lochu).
    // Włączany jest wyłącznie On-Demand (sturlanie, wybuch, rzut) w fazie TG_PrePhysics.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    // 1. Centralna konfiguracja replikacji sieciowej i optymalizacji pasma
    NetUtils::SetupQuantizedPhysicsReplication(this);

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    RootComponent = MeshComponent;

    // 2. Symulacja sztywnej bryły Chaos z ciągłą detekcją kolizji (CCD)
    MeshComponent->SetSimulatePhysics(true);
    MeshComponent->SetNotifyRigidBodyCollision(true);
    MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
    MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
    MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
    MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    MeshComponent->SetUseCCD(true);

    // 3. Propy fizyczne nie powinny wywoływać automatycznego wchodzenia na nie schodkiem (StepUp)
    // Zapobiega to desynchronizacji sieciowej, drganiom kamery i gubieniu gruntu pod stopami.
    MeshComponent->CanCharacterStepUpOn = ECB_No;

    // 4. Tłumienie kątowe i liniowe stabilizujące fizykę brył
    MeshComponent->SetLinearDamping(0.8f);
    MeshComponent->SetAngularDamping(5.0f);

    DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));
    StatusEffectComponent = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("StatusEffectComponent"));
}

void AInteractivePropBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AInteractivePropBase, CarryingActor);
    DOREPLIFETIME(AInteractivePropBase, RepLaunchVelocity);
}

void AInteractivePropBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    if (MeshComponent)
    {
        MeshComponent->OnComponentHit.AddDynamic(this, &AInteractivePropBase::HandleComponentHit);
        MeshComponent->OnComponentWake.AddDynamic(this, &AInteractivePropBase::HandleComponentWake);
        MeshComponent->OnComponentSleep.AddDynamic(this, &AInteractivePropBase::HandleComponentSleep);
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
    REQUIRE_AUTHORITY();

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
    return bCanBeGrabbed && bIsAlive && (CarryingActor == nullptr);
}

void AInteractivePropBase::OnGrabbed(AActor* Grabber)
{
    REQUIRE_AUTHORITY();

    SetActorTickEnabled(false);
    CarryingActor = Grabber;
    RepLaunchVelocity = FVector_NetQuantize::ZeroVector;
    NetUtils::AttachCarriedProp(this, MeshComponent, Grabber);
}

void AInteractivePropBase::OnDropped(AActor* Dropper, const FVector& LaunchVelocity)
{
    REQUIRE_AUTHORITY();

    CarryingActor = nullptr;
    RepLaunchVelocity = LaunchVelocity;

    NetUtils::DetachCarriedProp(this, MeshComponent, Dropper, LaunchVelocity);

    // Aktywujemy Tick na czas lotu rzuconego lub upuszczonego z pędem obiektu
    if (!LaunchVelocity.IsNearlyZero(KineticConfig::RestSpeedThreshold))
    {
        SetActorTickEnabled(true);
    }
}

void AInteractivePropBase::OnRep_CarryingActor()
{
    // Klient aktualizuje lokalny stan podpięcia na bazie autorytatywnego stanu serwera
    if (CarryingActor)
    {
        LastCarryingActor = CarryingActor;
        NetUtils::AttachCarriedProp(this, MeshComponent, CarryingActor);
    }
    else
    {
        // Klient odłącza propa i natychmiast aplikuje zreplikowany wektor lotu (eliminacja opadania pionowo w dół)
        NetUtils::DetachCarriedProp(this, MeshComponent, LastCarryingActor, RepLaunchVelocity);
        LastCarryingActor = nullptr;
    }
}

float AInteractivePropBase::GetMass() const
{
    return MeshComponent ? MeshComponent->GetMass() : 0.0f;
}

void AInteractivePropBase::HandleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                             UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                             const FHitResult& Hit)
{
    // Obliczenia kinetyczne, uszkodzenia i odrzuty wykonuje WYŁĄCZNIE serwer
    REQUIRE_AUTHORITY();

    if (!MeshComponent || CarryingActor != nullptr || !OtherActor || OtherActor == this)
    {
        return;
    }

    // Jeśli obiekt uderzający jest aktualnie trzymany przez postać - ignorujemy ocieranie
    if (const IGrabbableInterface* Grabbable = Cast<IGrabbableInterface>(OtherActor))
    {
        if (Grabbable->IsGrabbed())
        {
            return;
        }
    }

    // Zunifikowana obsługa zderzenia kinetycznego i punch-through
    UKineticForceLibrary::HandleKineticImpactAndPunchThrough(
        this,
        MeshComponent,
        OtherActor,
        OtherComp,
        Hit,
        NormalImpulse);
}

void AInteractivePropBase::HandleOnDestroyed(AActor* DestroyedActor)
{
    REQUIRE_AUTHORITY();

    UE_LOG(LogDungeonPhysics, Error, TEXT("[PropEntity]%s Object destroyed via Event: %s"), 
        *NetUtils::GetNetRolePrefix(this), *GetName());

    Destroy();
}

void AInteractivePropBase::HandleComponentWake(UPrimitiveComponent* WakingComponent, FName BoneName)
{
    // Budzimy Tick TYLKO na serwerze, gdy bryła zostaje wprawiona w ruch (sturlanie, wybuch, rzut, knockback)
    if (NetUtils::HasAuthority(this))
    {
        SetActorTickEnabled(true);
    }
}

void AInteractivePropBase::HandleComponentSleep(UPrimitiveComponent* SleepingComponent, FName BoneName)
{
    // Wyłączamy Tick (0% CPU), gdy bryła wyhamuje i zaśnie na posadzce
    if (NetUtils::HasAuthority(this))
    {
        SetActorTickEnabled(false);
    }
}

void AInteractivePropBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    REQUIRE_AUTHORITY();

    if (!MeshComponent || !MeshComponent->IsSimulatingPhysics() || CarryingActor != nullptr)
    {
        SetActorTickEnabled(false);
        return;
    }

    const FVector Velocity = MeshComponent->GetPhysicsLinearVelocity();
    const float SpeedSq = Velocity.SizeSquared();

    constexpr float MinSweepSpeedSq = KineticConfig::MinFlightSpeedForSweep * KineticConfig::MinFlightSpeedForSweep;
    constexpr float RestSpeedSq = KineticConfig::RestSpeedThreshold * KineticConfig::RestSpeedThreshold;

    // Jeśli prop porusza się z prędkością zdolną do zadania obrażeń kinetycznych
    if (SpeedSq >= MinSweepSpeedSq)
    {
        UKineticForceLibrary::PerformPreImpactSweep(this, MeshComponent, DeltaTime, KineticConfig::MinFlightSpeedForSweep);
    }
    else if (SpeedSq < RestSpeedSq)
    {
        // Obiekt prawie się zatrzymał -> wyłączamy tick, by nie marnować zasobów CPU
        SetActorTickEnabled(false);
    }
}


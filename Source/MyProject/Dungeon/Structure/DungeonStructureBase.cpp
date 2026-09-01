#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Interaction/Interfaces/IGrabbableInterface/IGrabbableInterface.h"

ADungeonStructureBase::ADungeonStructureBase()
{
	PrimaryActorTick.bCanEverTick = false;

	StructureMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StructureMesh"));
	RootComponent = StructureMesh;

	// Domyślnie architektura lochu jest stabilna, statyczna, bez fizyki
	StructureMesh->SetSimulatePhysics(false);
	StructureMesh->SetNotifyRigidBodyCollision(true);
	StructureMesh->SetCollisionProfileName(TEXT("BlockAll"));
	StructureMesh->SetGenerateOverlapEvents(false);
	StructureMesh->CanCharacterStepUpOn = ECB_Yes;

	DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));

	MaterialType = EPhysicalMaterialType::Stone;
	bIsDestructible = false;
}

void ADungeonStructureBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (StructureMesh)
	{
		StructureMesh->OnComponentHit.AddDynamic(this, &ADungeonStructureBase::HandleComponentHit);
	}

	if (DamageableComponent)
	{
		DamageableComponent->OnDestroyed.AddDynamic(this, &ADungeonStructureBase::HandleOnDestroyed);
	}
}

void ADungeonStructureBase::BeginPlay()
{
	Super::BeginPlay();

	// Jeśli struktura jest oznaczona jako niezniszczalna, wyłączamy nasłuchiwanie zderzeń i niszczenie
	if (!bIsDestructible && DamageableComponent)
	{
		DamageableComponent->SetComponentTickEnabled(false);
	}
}

EPhysicalMaterialType ADungeonStructureBase::GetMaterialType_Implementation() const
{
	return MaterialType;
}

void ADungeonStructureBase::HandleComponentHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!bIsDestructible || !DamageableComponent || DamageableComponent->IsDestroyed())
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

	// 2. Pobieramy prędkość uderzającego obiektu
	FVector IncomingVelocity = FVector::ZeroVector;

	if (OtherComp && OtherComp->IsSimulatingPhysics())
	{
		IncomingVelocity = OtherComp->GetPhysicsLinearVelocity();
	}
	else if (OtherActor)
	{
		IncomingVelocity = OtherActor->GetVelocity();
	}

	// 3. Sprawdzamy prędkość wnikającą prostopadle w strukturę (Dot Product z Normalną)
	// Normalna wskazuje na zewnątrz ściany, więc wektor prędkości wnikającej ma z nią ujemny iloczyn skalarny
	const float ImpactSpeed = -FVector::DotProduct(IncomingVelocity, Hit.ImpactNormal);

	// 4. Jeśli obiekt faktycznie uderza w strukturę prostopadle
	if (ImpactSpeed > 0.0f)
	{
		DamageableComponent->ApplyKineticImpact(ImpactSpeed);
	}
}

void ADungeonStructureBase::HandleOnDestroyed(AActor* DestroyedActor)
{
	// Spawnowanie opcjonalnego gruzu / efektu cząsteczkowego
	if (DestroyedDebrisClass && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AActor>(
			DestroyedDebrisClass,
			GetActorTransform(),
			SpawnParams);
	}

	UE_LOG(LogTemp, Warning, TEXT("[DungeonStructure] %s destroyed!"), *GetName());
	Destroy();
}

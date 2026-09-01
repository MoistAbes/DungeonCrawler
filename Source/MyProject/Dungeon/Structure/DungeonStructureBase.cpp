#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"

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

	// Obliczamy prędkość uderzającego obiektu (z dowolnego kierunku: góra, dół, boki)
	float ImpactSpeed = 0.0f;

	if (OtherComp && OtherComp->IsSimulatingPhysics())
	{
		ImpactSpeed = OtherComp->GetPhysicsLinearVelocity().Size();
	}
	else if (OtherActor)
	{
		ImpactSpeed = OtherActor->GetVelocity().Size();
	}

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

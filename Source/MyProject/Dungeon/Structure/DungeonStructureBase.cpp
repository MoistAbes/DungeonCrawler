#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"
#include "MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h"

ADungeonStructureBase::ADungeonStructureBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// Włączamy replikację cyklu życia aktora (niszczenie/znikanie w sieci bez replikacji transformu ruchu)
	SetReplicates(true);

	StructureMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StructureMesh"));
	RootComponent = StructureMesh;

	// Domyślnie architektura lochu jest stabilna, statyczna, bez fizyki
	StructureMesh->SetSimulatePhysics(false);
	StructureMesh->SetNotifyRigidBodyCollision(false);
	StructureMesh->SetCollisionProfileName(TEXT("BlockAll"));
	StructureMesh->SetGenerateOverlapEvents(false);
	StructureMesh->CanCharacterStepUpOn = ECB_Yes;

	DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));

	MaterialType = EPhysicalMaterialType::Stone;
	bIsDestructible = false;
	DamageableComponent->SetInvulnerable(true);
}

void ADungeonStructureBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Zdarzenia kolizji i niszczenia podpinamy wyłącznie wtedy, gdy struktura może ulec zniszczeniu
	DamageableComponent->SetInvulnerable(!bIsDestructible);

	if (bIsDestructible)
	{
		StructureMesh->SetNotifyRigidBodyCollision(true);
		StructureMesh->OnComponentHit.AddDynamic(this, &ADungeonStructureBase::HandleComponentHit);
		DamageableComponent->OnDestroyed.AddDynamic(this, &ADungeonStructureBase::HandleOnDestroyed);
	}
	else
	{
		DamageableComponent->SetComponentTickEnabled(false);
	}
}

void ADungeonStructureBase::BeginPlay()
{
	Super::BeginPlay();
}

void ADungeonStructureBase::HandleComponentHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	REQUIRE_AUTHORITY();

	if (!bIsDestructible || !OtherActor || OtherActor == this)
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

	UKineticForceLibrary::HandleKineticImpactAndPunchThrough(
		OtherActor,
		OtherComp,
		this,
		StructureMesh,
		Hit,
		NormalImpulse,
		PunchThroughVelocityRetention);
}

void ADungeonStructureBase::HandleOnDestroyed(AActor* DestroyedActor)
{
	REQUIRE_AUTHORITY();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UE_LOG(LogDungeonPhysics, Warning, TEXT("[DungeonStructure]%s %s has collapsed and been destroyed!"),
		*NetUtils::GetNetRolePrefix(this), *GetName());

	// 0. Czyszczenie komórek powierzchniowych w zniszczonym obszarze fundamentu
	ClearSurfaceGrid(World);

	// 1. Zabezpieczenie: natychmiastowe usunięcie kolizji bryły i widoczności
	StructureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StructureMesh->SetVisibility(false);

	// 2. Bezpieczne niszczenie podpiętych aktorów (np. stref powierzchniowych)
	DestroyAttachedActors();

	// 3. Spawnowanie opcjonalnego gruzu / efektu cząsteczkowego
	SpawnDebris(World);

	// 4. Po krótkiej chwili na dokończenie replikacji niszczymy aktora
	SetLifeSpan(0.1f);
}

void ADungeonStructureBase::ClearSurfaceGrid(UWorld* World)
{
	if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
	{
		const FBox StructureBounds = StructureMesh->Bounds.GetBox();
		SurfaceSubsystem->ClearCellsInBounds(StructureBounds);
	}
}

void ADungeonStructureBase::DestroyAttachedActors()
{
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		if (Attached && !Attached->IsActorBeingDestroyed())
		{
			Attached->Destroy();
		}
	}
}

void ADungeonStructureBase::SpawnDebris(UWorld* World)
{
	if (DestroyedDebrisClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AActor>(DestroyedDebrisClass, GetActorTransform(), SpawnParams);
	}
}

#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
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

	if (DamageableComponent->IsDestroyed())
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

	// 2. Obliczamy prędkość uderzenia prostopadłego przez zunifikowaną bibliotekę kinetyczną
	const float ImpactSpeed = UKineticForceLibrary::CalculateImpactSpeed(StructureMesh, OtherActor, OtherComp, Hit.ImpactNormal);

	UE_LOG(LogDungeonPhysics, Log, TEXT("[DungeonStructure]%s %s hit by %s | ImpactSpeed: %.1f cm/s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(), OtherActor ? *OtherActor->GetName() : TEXT("None"), ImpactSpeed);

	// 3. Jeśli obiekt faktycznie uderza w strukturę prostopadle z prędkością powyżej progu
	if (ImpactSpeed > 0.0f)
	{
		DamageableComponent->ApplyKineticImpact(ImpactSpeed);
	}
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

	// 1. Natychmiastowe usunięcie kolizji bryły i widoczności, by przepuścić obiekty w locie
	StructureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StructureMesh->SetVisibility(false);

	// 2. Bezpieczne niszczenie podpiętych aktorów (np. stref powierzchniowych), by nie lewitowały w powietrzu
	DestroyAttachedActors();

	// 3. Mechanika Punch-Through: postacie pędzące w wyrwę kontynuują bieg z zachowaniem części pędu
	ApplyPunchThrough(World);

	// 4. Spawnowanie opcjonalnego gruzu / efektu cząsteczkowego
	SpawnDebris(World);

	// 5. Po krótkiej chwili (na dokończenie ewentualnych replikacji) niszczymy aktora
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

void ADungeonStructureBase::ApplyPunchThrough(UWorld* World)
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(100.0f, 100.0f, 150.0f));
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PunchThroughQuery), false, this);

	if (World->OverlapMultiByChannel(Overlaps, GetActorLocation(), GetActorRotation().Quaternion(), ECC_Pawn, BoxShape, QueryParams))
	{
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (ACharacter* Character = Cast<ACharacter>(Overlap.GetActor()))
			{
				const FVector PenetrationVelocity = Character->GetVelocity() * PunchThroughVelocityRetention;
				Character->LaunchCharacter(PenetrationVelocity, true, true);

				UE_LOG(LogDungeonPhysics, Log, TEXT("[DungeonStructure] Punch-Through: Character %s penetrated destroyed wall with velocity %s"),
					*Character->GetName(), *Character->GetVelocity().ToString());
			}
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

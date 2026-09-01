#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
	PunchThroughVelocityRetention = 0.6f;
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

	// 2. Pobieramy prędkość uderzającego obiektu (ze wsparciem dla CharacterMovementComponent)
	FVector IncomingVelocity = FVector::ZeroVector;

	if (const ACharacter* Character = Cast<ACharacter>(OtherActor))
	{
		if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
		{
			IncomingVelocity = CMC->GetLastUpdateVelocity();
		}
		else
		{
			IncomingVelocity = Character->GetVelocity();
		}
	}
	else if (OtherComp && OtherComp->IsSimulatingPhysics())
	{
		IncomingVelocity = OtherComp->GetPhysicsLinearVelocity();
	}
	else if (OtherActor)
	{
		IncomingVelocity = OtherActor->GetVelocity();
	}

	// 3. Sprawdzamy prędkość prostopadłą do powierzchni zderzenia
	const float ImpactSpeed = FMath::Abs(FVector::DotProduct(IncomingVelocity, Hit.ImpactNormal));

	UE_LOG(LogTemp, Log, TEXT("[DungeonStructure] %s hit by %s | IncVel: %s | ImpactSpeed: %.1f cm/s"),
		*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("None"), *IncomingVelocity.ToString(), ImpactSpeed);

	// 4. Jeśli obiekt faktycznie uderza w strukturę prostopadle z prędkością powyżej progu
	if (ImpactSpeed > 0.0f)
	{
		DamageableComponent->ApplyKineticImpact(ImpactSpeed);

		// 5. Punch-Through: Jeśli uderzenie zniszczyło strukturę, przekazujemy pęd dalej za ścianę
		if (DamageableComponent->IsDestroyed())
		{
			if (StructureMesh)
			{
				StructureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			if (PunchThroughVelocityRetention > 0.0f && !IncomingVelocity.IsNearlyZero())
			{
				const FVector ContinuedVelocity = IncomingVelocity * PunchThroughVelocityRetention;

				if (ACharacter* Character = Cast<ACharacter>(OtherActor))
				{
					Character->LaunchCharacter(ContinuedVelocity, true, true);
					UE_LOG(LogTemp, Warning, TEXT("[DungeonStructure] Punch-Through! %s launches past broken structure with Velocity: %s"),
						*Character->GetName(), *ContinuedVelocity.ToString());
				}
				else if (OtherComp && OtherComp->IsSimulatingPhysics())
				{
					OtherComp->SetPhysicsLinearVelocity(ContinuedVelocity);
				}
			}
		}
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

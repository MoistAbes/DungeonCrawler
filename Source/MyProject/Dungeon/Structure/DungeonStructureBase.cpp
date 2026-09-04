#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"

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
	StatusEffectComponent = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("StatusEffectComponent"));

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
	}
}

void ADungeonStructureBase::HandleOnDestroyed(AActor* DestroyedActor)
{
	if (!bIsDestructible)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[DungeonStructure] %s destroyed!"), *GetName());

	// 1. Opcjonalny spawn gruzu / VFX
	if (DestroyedDebrisClass && GetWorld())
	{
		GetWorld()->SpawnActor<AActor>(DestroyedDebrisClass, GetActorTransform());
	}

	// 2. Wyłączamy natychmiast kolizję, aby gracze i pociski przelatywali przez otwór
	if (StructureMesh)
	{
		StructureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StructureMesh->SetVisibility(false);
	}

	// 3. Punch-Through: jeśli w pobliżu znajduje się aktor o dużym pędzie (np. rzucony gracz),
	// pozwalamy mu przelecieć dalej z zachowaniem części pędu
	if (GetWorld() && PunchThroughVelocityRetention > 0.0f)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(200.0f);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StructurePunchThrough), false, this);

		GetWorld()->OverlapMultiByChannel(Overlaps, GetActorLocation(), FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (ACharacter* Character = Cast<ACharacter>(Overlap.GetActor()))
			{
				const FVector PrevVelocity = Character->GetCharacterMovement() ? Character->GetCharacterMovement()->Velocity : Character->GetVelocity();
				if (PrevVelocity.SizeSquared() > 10000.0f)
				{
					const FVector RetainedVelocity = PrevVelocity * PunchThroughVelocityRetention;
					Character->LaunchCharacter(RetainedVelocity, true, true);
					UE_LOG(LogTemp, Warning, TEXT("[DungeonStructure] Punch-Through! %s launches past broken structure with Velocity: %s"),
						*Character->GetName(), *RetainedVelocity.ToString());
				}
			}
		}
	}

	// 4. Usunięcie aktora
	SetLifeSpan(0.1f);
}

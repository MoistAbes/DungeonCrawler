#include "DungeonStructureBase.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
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

	// 2. Obliczamy prędkość uderzenia prostopadłego przez zunifikowaną bibliotekę kinetyczną
	const float ImpactSpeed = UKineticForceLibrary::CalculateImpactSpeed(StructureMesh, OtherActor, OtherComp, Hit.ImpactNormal);

	UE_LOG(LogTemp, Log, TEXT("[DungeonStructure] %s hit by %s | ImpactSpeed: %.1f cm/s"),
		*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("None"), ImpactSpeed);

	// 3. Jeśli obiekt faktycznie uderza w strukturę prostopadle z prędkością powyżej progu
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

	UE_LOG(LogTemp, Warning, TEXT("[DungeonStructure] %s has collapsed and been destroyed!"), *GetName());

	// -------------------------------------------------------------------------------------------------
	// MECHANIKA PUNCH-THROUGH (Przebijanie barykady):
	// Jeśli postać lub pocisk/obiekt fizyczny przebił tę ścianę z dużą prędkością, chcemy aby nie został
	// natychmiast zatrzymany w miejscu przez nagłą kolizję, lecz kontynuował ruch przez powstałą wyrwę.
	// -------------------------------------------------------------------------------------------------

	// 1. Natychmiastowe usunięcie kolizji bryły, by przepuścić obiekty w locie
	if (StructureMesh)
	{
		StructureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StructureMesh->SetVisibility(false);
	}

	// 2. Wykrywamy obiekty w bezpośrednim punkcie zniszczenia, by zredukować ich prędkość jedynie częściowo
	if (UWorld* World = GetWorld())
	{
		const FVector StructureCenter = GetActorLocation();
		TArray<FOverlapResult> Overlaps;
		FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(100.0f, 100.0f, 150.0f));
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PunchThroughQuery), false, this);

		if (World->OverlapMultiByChannel(Overlaps, StructureCenter, GetActorRotation().Quaternion(), ECC_Pawn, BoxShape, QueryParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (ACharacter* Character = Cast<ACharacter>(Overlap.GetActor()))
				{
					// Przekazujemy pęd z redukcją oporu przebicia
					const FVector CurrentVel = Character->GetVelocity();
					Character->LaunchCharacter(CurrentVel * PunchThroughVelocityRetention, true, true);
				}
			}
		}
	}

	// 3. Ostateczne usunięcie aktora
	Destroy();
}

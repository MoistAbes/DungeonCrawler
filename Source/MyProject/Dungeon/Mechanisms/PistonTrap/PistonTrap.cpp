#include "PistonTrap.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"

APistonTrap::APistonTrap()
{
	// Optymalizacja Zero-Tick: włączamy Tick wyłącznie podczas fizycznego suwu głowicy
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MaterialType = EPhysicalMaterialType::Stone;

	// Ruchoma głowica taranu / tłoka
	PistonHeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PistonHeadMesh"));
	PistonHeadMesh->SetupAttachment(BaseMeshComponent);
	PistonHeadMesh->SetSimulatePhysics(false);
	PistonHeadMesh->SetCollisionProfileName(TEXT("BlockAll"));
	PistonHeadMesh->CanCharacterStepUpOn = ECB_Yes;

	// Strefa uderzenia kinetycznego na czole głowicy
	DamageBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageBox"));
	DamageBox->SetupAttachment(PistonHeadMesh);
	DamageBox->SetBoxExtent(FVector(20.0f, 60.0f, 60.0f));
	DamageBox->SetRelativeLocation(FVector(80.0f, 0.0f, 0.0f));
	DamageBox->SetCollisionObjectType(ECC_WorldDynamic);
	DamageBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	DamageBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DamageBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	DamageBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	DamageBox->SetGenerateOverlapEvents(true);

	PushDirection = FVector(1.0f, 0.0f, 0.0f);
	StrokeDistance = 300.0f;
	ExtendSpeed = 1200.0f;
	RetractSpeed = 150.0f;
	RetractDelay = 0.5f;

	KnockbackSpeed = 1800.0f;
	BaseImpactDamage = 40.0f;
	PhysicsImpulseMultiplier = 1500.0f;

	PistonState = EPistonState::IdleAtHome;
	CurrentStrokeDistance = 0.0f;
}

void APistonTrap::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APistonTrap, PistonState);
}

void APressurePlateOverlapSetup() {}

void APistonTrap::BeginPlay()
{
	Super::BeginPlay();

	DefaultHeadRelativeLocation = PistonHeadMesh->GetRelativeLocation();
	NormalizedPushDirection = PushDirection.GetSafeNormal();

	if (DamageBox)
	{
		DamageBox->OnComponentBeginOverlap.AddDynamic(this, &APistonTrap::HandleDamageBoxBeginOverlap);
	}
}

void APistonTrap::ExecuteTrapAction(AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	// Jeśli tłok już wykonuje cykl, ignorujemy kolejne wywołanie
	if (PistonState != EPistonState::IdleAtHome)
	{
		return;
	}

	HitActorsThisStroke.Empty();
	PistonState = EPistonState::Extending;

	// Włączamy Tick na czas ruchu
	SetActorTickEnabled(true);
}

void APistonTrap::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!PistonHeadMesh)
	{
		SetActorTickEnabled(false);
		return;
	}

	if (PistonState == EPistonState::Extending)
	{
		CurrentStrokeDistance += ExtendSpeed * DeltaTime;

		if (CurrentStrokeDistance >= StrokeDistance)
		{
			CurrentStrokeDistance = StrokeDistance;
			PistonState = EPistonState::HoldingAtExtended;

			// Wyłączamy Tick na czas pauzy (Zero-Tick)
			SetActorTickEnabled(false);

			if (HasAuthority())
			{
				if (UWorld* World = GetWorld())
				{
					World->GetTimerManager().SetTimer(
						RetractTimerHandle,
						this,
						&APistonTrap::BeginRetracting,
						RetractDelay,
						false);
				}
			}
		}

		PistonHeadMesh->SetRelativeLocation(DefaultHeadRelativeLocation + (NormalizedPushDirection * CurrentStrokeDistance));
	}
	else if (PistonState == EPistonState::Retracting)
	{
		CurrentStrokeDistance -= RetractSpeed * DeltaTime;

		if (CurrentStrokeDistance <= 0.0f)
		{
			CurrentStrokeDistance = 0.0f;
			PistonState = EPistonState::IdleAtHome;

			// Koniec cyklu - wyłączamy Tick (Zero-Tick)
			SetActorTickEnabled(false);
		}

		PistonHeadMesh->SetRelativeLocation(DefaultHeadRelativeLocation + (NormalizedPushDirection * CurrentStrokeDistance));
	}
	else
	{
		// W stanach spoczynku lub pauzy wyłączamy Tick
		SetActorTickEnabled(false);
	}
}

void APistonTrap::BeginRetracting()
{
	REQUIRE_AUTHORITY();

	PistonState = EPistonState::Retracting;
	SetActorTickEnabled(true);
}

void APistonTrap::HandleDamageBoxBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	REQUIRE_AUTHORITY();

	// Obrażenia i odrzut zadajemy wyłącznie w fazie gwałtownego wysunięcia (Extending)
	if (PistonState != EPistonState::Extending)
	{
		return;
	}

	ApplyKineticHit(OtherActor, OtherComp);
}

void APistonTrap::ApplyKineticHit(AActor* HitActor, UPrimitiveComponent* HitComponent)
{
	REQUIRE_AUTHORITY();

	if (!HitActor || HitActor == this || HitActorsThisStroke.Contains(HitActor))
	{
		return;
	}

	HitActorsThisStroke.Add(HitActor);

	const FVector WorldPushDir = GetActorTransform().TransformVectorNoScale(NormalizedPushDirection).GetSafeNormal();

	UE_LOG(LogTemp, Log, TEXT("[PistonTrap]%s %s rammed actor: %s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(), *HitActor->GetName());

	// 1. Postać gracza / AI (Kinematyczna CMC)
	if (ACharacter* Character = Cast<ACharacter>(HitActor))
	{
		FVector LaunchVelocity = WorldPushDir * KnockbackSpeed;
		// Jeśli tłok uderza horyzontalnie, dodajemy lekkie uniesienie w górę (loft), by oderwać postać od tarcia posadzki
		if (FMath::Abs(WorldPushDir.Z) < 0.2f)
		{
			LaunchVelocity.Z = FMath::Max(LaunchVelocity.Z, 350.0f);
		}

		Character->LaunchCharacter(LaunchVelocity, true, true);

		if (UDamageableComponent* DamComp = Character->FindComponentByClass<UDamageableComponent>())
		{
			DamComp->ApplyDamage(BaseImpactDamage);
		}
	}
	// 2. Obiekt fizyczny Chaos (głaz, skrzynia, beczka)
	else if (HitComponent && HitComponent->IsSimulatingPhysics())
	{
		const float BodyMass = HitComponent->GetMass();
		const FVector Impulse = WorldPushDir * (BodyMass * KnockbackSpeed);
		HitComponent->AddImpulse(Impulse, NAME_None, false);

		if (UDamageableComponent* DamComp = HitActor->FindComponentByClass<UDamageableComponent>())
		{
			DamComp->ApplyKineticImpact(ExtendSpeed);
		}
	}
	// 3. Statyczna przeszkoda niszczalna (np. barykada, osłabiona ściana)
	else
	{
		if (UDamageableComponent* DamComp = HitActor->FindComponentByClass<UDamageableComponent>())
		{
			DamComp->ApplyKineticImpact(ExtendSpeed);
		}
	}
}

void APistonTrap::OnRep_PistonState()
{
	switch (PistonState)
	{
	case EPistonState::Extending:
	case EPistonState::Retracting:
		SetActorTickEnabled(true);
		break;

	case EPistonState::HoldingAtExtended:
		CurrentStrokeDistance = StrokeDistance;
		if (PistonHeadMesh)
		{
			PistonHeadMesh->SetRelativeLocation(DefaultHeadRelativeLocation + (NormalizedPushDirection * StrokeDistance));
		}
		SetActorTickEnabled(false);
		break;

	case EPistonState::IdleAtHome:
		CurrentStrokeDistance = 0.0f;
		if (PistonHeadMesh)
		{
			PistonHeadMesh->SetRelativeLocation(DefaultHeadRelativeLocation);
		}
		SetActorTickEnabled(false);
		break;
	}
}

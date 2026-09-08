#include "PressurePlateProp.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"

APressurePlateProp::APressurePlateProp()
{
	// Optymalizacja Zero-Tick: włączamy Tick tylko na czas animacji zapadania kafla
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MaterialType = EPhysicalMaterialType::Stone;

	// MeshComponent z klasy bazowej służy jako nieruchoma zewnętrzna rama/krawędź płyty
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	MeshComponent->CanCharacterStepUpOn = ECB_Yes;

	// Ruchomy kafel płyty
	PlateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlateMesh"));
	PlateMesh->SetupAttachment(MeshComponent);
	PlateMesh->SetSimulatePhysics(false);
	PlateMesh->SetCollisionProfileName(TEXT("BlockAll"));
	PlateMesh->CanCharacterStepUpOn = ECB_Yes;

	// Objętość detekcji ciał fizycznych i graczy (Overlap)
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(MeshComponent);
	TriggerBox->SetBoxExtent(FVector(80.0f, 80.0f, 20.0f));
	TriggerBox->SetRelativeLocation(FVector(0.0f, 0.0f, 25.0f));
	TriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	TriggerBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);

	RequiredMass = 50.0f;
	DefaultCharacterMass = 80.0f;
	DepressionDepth = 6.0f;
	PlateMoveSpeed = 30.0f;
}

void APressurePlateProp::BeginPlay()
{
	Super::BeginPlay();

	DefaultPlateZ = PlateMesh->GetRelativeLocation().Z;
	TargetPlateZ = DefaultPlateZ;

	// Nasłuchiwanie wejścia w strefę (tylko serwer przetwarza logikę)
	if (TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &APressurePlateProp::HandleTriggerBeginOverlap);
		TriggerBox->OnComponentEndOverlap.AddDynamic(this, &APressurePlateProp::HandleTriggerEndOverlap);
	}
}

void APressurePlateProp::HandleTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	REQUIRE_AUTHORITY();

	if (!OtherActor || OtherActor == this || !OtherComp)
	{
		return;
	}

	float Mass = 0.0f;

	// 1. Postać (kinematyczna z CMC)
	if (const ACharacter* Character = Cast<ACharacter>(OtherActor))
	{
		Mass = DefaultCharacterMass;
		if (Character->GetCharacterMovement())
		{
			Mass = FMath::Max(Mass, Character->GetCharacterMovement()->Mass);
		}
	}
	// 2. Obiekt fizyczny Chaos (skrzynia, głaz)
	else if (OtherComp->IsSimulatingPhysics())
	{
		Mass = OtherComp->GetMass();
	}
	else
	{
		Mass = OtherComp->GetMass();
	}

	if (Mass > 0.0f)
	{
		OverlappingComponents.Add(OtherComp, Mass);
		LastTriggeringActor = OtherActor;
		RecalculateMassAndEvaluate();
	}
}

void APressurePlateProp::HandleTriggerEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	REQUIRE_AUTHORITY();

	if (!OtherComp)
	{
		return;
	}

	if (OverlappingComponents.Contains(OtherComp))
	{
		OverlappingComponents.Remove(OtherComp);
		RecalculateMassAndEvaluate();
	}
}

void APressurePlateProp::RecalculateMassAndEvaluate()
{
	REQUIRE_AUTHORITY();

	float NewTotalMass = 0.0f;

	// Oczyszczamy nieprawidłowe wskaźniki (np. zniszczone wazy/beczki) i sumujemy masę
	for (auto It = OverlappingComponents.CreateIterator(); It; ++It)
	{
		if (It.Key().IsValid())
		{
			NewTotalMass += It.Value();
		}
		else
		{
			It.RemoveCurrent();
		}
	}

	CurrentTotalMass = NewTotalMass;

	UE_LOG(LogDungeonMechanisms, Verbose, TEXT("[PressurePlate]%s %s current total mass: %.1f / %.1f kg"),
		*NetUtils::GetNetRolePrefix(this), *GetName(), CurrentTotalMass, RequiredMass);

	const bool bShouldBeActive = (CurrentTotalMass >= RequiredMass);
	SetActiveState(bShouldBeActive, LastTriggeringActor.Get());
}

void APressurePlateProp::HandleStateChanged(bool bNewState, AActor* TriggeringActor)
{
	Super::HandleStateChanged(bNewState, TriggeringActor);

	// Określamy cel dla lokalnej wysokości kafla Z
	TargetPlateZ = bNewState ? (DefaultPlateZ - DepressionDepth) : DefaultPlateZ;

	// Uruchamiamy Tick na żądanie (on-demand), by płynnie dosunąć kafel
	SetActorTickEnabled(true);
}

void APressurePlateProp::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!PlateMesh)
	{
		SetActorTickEnabled(false);
		return;
	}

	FVector CurrentRelativeLocation = PlateMesh->GetRelativeLocation();
	const float NewZ = FMath::FInterpConstantTo(CurrentRelativeLocation.Z, TargetPlateZ, DeltaTime, PlateMoveSpeed);
	CurrentRelativeLocation.Z = NewZ;
	PlateMesh->SetRelativeLocation(CurrentRelativeLocation);

	// Po dotarciu do celu wyłączamy Tick (Zero-Tick)
	if (FMath::IsNearlyEqual(NewZ, TargetPlateZ, 0.05f))
	{
		CurrentRelativeLocation.Z = TargetPlateZ;
		PlateMesh->SetRelativeLocation(CurrentRelativeLocation);
		SetActorTickEnabled(false);
	}
}

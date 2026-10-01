#include "DungeonGateProp.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"

ADungeonGateProp::ADungeonGateProp()
{
	// Optymalizacja Zero-Tick: Tick aktywny wyłącznie podczas fizycznego ruchu kraty
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	bReplicates = true;

	MaterialType = EPhysicalMaterialType::Metal;

	// Nieruchomy korzeń aktora w przestrzeni świata (Z = 0 na poziomie posadzki)
	SceneRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRootComponent"));
	RootComponent = SceneRootComponent;

	// Ruchoma krata lub wrota
	GateMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateMeshComponent"));
	GateMeshComponent->SetupAttachment(RootComponent);
	GateMeshComponent->SetSimulatePhysics(false);
	GateMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	GateMeshComponent->CanCharacterStepUpOn = ECB_No;

	// Komponent integralności i zniszczeń
	DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));
	DamageableComponent->SetInvulnerable(true); // Domyślnie brama żelazna jest niezniszczalna

	// Domyślne parametry kinematyczne
	OpenOffset = FVector(0.0f, 0.0f, 280.0f);
	OpenSpeed = 150.0f;
	CloseSpeed = 350.0f;
	bAutoClose = false;
	AutoCloseDelay = 5.0f;
	bStartOpen = false;

	// Anti-Crush
	BlockBehavior = EGateBlockBehavior::WaitAndCrush;
	CrushDamage = 35.0f;
	CrushDamageInterval = 0.5f;
	ReboundDelay = 1.5f;

	GateState = EGateState::Closed;
}

void ADungeonGateProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADungeonGateProp, GateState);
}

void ADungeonGateProp::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (DamageableComponent && bIsDestructible)
	{
		DamageableComponent->SetInvulnerable(false);
		DamageableComponent->OnDestroyed.AddDynamic(this, &ADungeonGateProp::HandleOnDestroyed);
	}
}

void ADungeonGateProp::BeginPlay()
{
	Super::BeginPlay();

	DefaultGateRelativeLocation = GateMeshComponent->GetRelativeLocation();

	if (bStartOpen)
	{
		GateState = EGateState::Open;
		GateMeshComponent->SetRelativeLocation(DefaultGateRelativeLocation + OpenOffset);
	}
	else
	{
		GateState = EGateState::Closed;
		GateMeshComponent->SetRelativeLocation(DefaultGateRelativeLocation);
	}

	SetActorTickEnabled(false);
}

void ADungeonGateProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoCloseTimerHandle);
		World->GetTimerManager().ClearTimer(ReboundTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ADungeonGateProp::SetMechanismState_Implementation(bool bActive, AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s %s received SetMechanismState(bActive=%s) from %s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(),
		bActive ? TEXT("true") : TEXT("false"),
		TriggeringActor ? *TriggeringActor->GetName() : TEXT("None"));

	if (bActive)
	{
		OpenGate(TriggeringActor);
	}
	else
	{
		CloseGate(TriggeringActor);
	}
}

void ADungeonGateProp::OpenGate(AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	if (GateState == EGateState::Open || GateState == EGateState::Opening)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReboundTimerHandle);
	}

	StartOpening(TriggeringActor);
}

void ADungeonGateProp::CloseGate(AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	if (GateState == EGateState::Closed || GateState == EGateState::Closing)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoCloseTimerHandle);
	}

	StartClosing(TriggeringActor);
}

void ADungeonGateProp::ToggleGate(AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	if (GateState == EGateState::Open || GateState == EGateState::Opening)
	{
		CloseGate(TriggeringActor);
	}
	else
	{
		OpenGate(TriggeringActor);
	}
}

void ADungeonGateProp::StartOpening(AActor* TriggeringActor)
{
	GateState = EGateState::Opening;
	LastTriggeringActor = TriggeringActor;

	SetActorTickEnabled(true);

	OnGateStateChanged.Broadcast(GateState, TriggeringActor);
	ReceiveGateStateChangedCosmetic(GateState);

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s %s started Opening"),
		*NetUtils::GetNetRolePrefix(this), *GetName());
}

void ADungeonGateProp::StartClosing(AActor* TriggeringActor)
{
	GateState = EGateState::Closing;
	LastTriggeringActor = TriggeringActor;

	SetActorTickEnabled(true);

	OnGateStateChanged.Broadcast(GateState, TriggeringActor);
	ReceiveGateStateChangedCosmetic(GateState);

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s %s started Closing"),
		*NetUtils::GetNetRolePrefix(this), *GetName());
}

void ADungeonGateProp::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GateMeshComponent)
	{
		SetActorTickEnabled(false);
		return;
	}

	const FVector CurrentLoc = GateMeshComponent->GetRelativeLocation();

	if (GateState == EGateState::Opening)
	{
		const FVector TargetLoc = DefaultGateRelativeLocation + OpenOffset;
		const FVector NewLoc = FMath::VInterpConstantTo(CurrentLoc, TargetLoc, DeltaTime, OpenSpeed);

		GateMeshComponent->SetRelativeLocation(NewLoc, false);

		if (NewLoc.Equals(TargetLoc, 1.0f))
		{
			GateMeshComponent->SetRelativeLocation(TargetLoc);
			HandleReachedOpen();
		}
	}
	else if (GateState == EGateState::Closing)
	{
		const FVector TargetLoc = DefaultGateRelativeLocation;
		const FVector DesiredLoc = FMath::VInterpConstantTo(CurrentLoc, TargetLoc, DeltaTime, CloseSpeed);

		// Zamykanie ze sweepem (Anti-Crush)
		FHitResult HitResult;
		GateMeshComponent->SetRelativeLocation(DesiredLoc, true, &HitResult);

		if (HitResult.bBlockingHit)
		{
			HandleBlockedByObstacle(HitResult, DeltaTime);
		}
		else if (GateMeshComponent->GetRelativeLocation().Equals(TargetLoc, 1.0f))
		{
			GateMeshComponent->SetRelativeLocation(TargetLoc);
			HandleReachedClosed();
		}
	}
	else
	{
		// W stanach spoczynku (Open / Closed) wyłączamy Tick (Zero-Tick)
		SetActorTickEnabled(false);
	}
}

void ADungeonGateProp::HandleReachedOpen()
{
	GateState = EGateState::Open;
	SetActorTickEnabled(false);

	OnGateStateChanged.Broadcast(GateState, LastTriggeringActor.Get());
	ReceiveGateStateChangedCosmetic(GateState);

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s %s reached Open state"),
		*NetUtils::GetNetRolePrefix(this), *GetName());

	if (HasAuthority() && bAutoClose)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				AutoCloseTimerHandle,
				this,
				&ADungeonGateProp::HandleAutoCloseTimer,
				AutoCloseDelay,
				false);
		}
	}
}

void ADungeonGateProp::HandleReachedClosed()
{
	GateState = EGateState::Closed;
	SetActorTickEnabled(false);

	OnGateStateChanged.Broadcast(GateState, LastTriggeringActor.Get());
	ReceiveGateStateChangedCosmetic(GateState);

	OnGateSlamDown.Broadcast();
	ReceiveGateSlamDownCosmetic();

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s %s reached Closed state (Slam Down)"),
		*NetUtils::GetNetRolePrefix(this), *GetName());
}

void ADungeonGateProp::HandleBlockedByObstacle(const FHitResult& HitResult, float DeltaTime)
{
	AActor* ObstacleActor = HitResult.GetActor();

	ReceiveGateBlockedCosmetic(HitResult);

	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogDungeonMechanisms, Verbose, TEXT("[GateProp]%s %s blocked by %s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(),
		ObstacleActor ? *ObstacleActor->GetName() : TEXT("Unknown"));

	// Zadajemy obrażenia miażdżące obiektowi blokującemu opadanie
	if (ObstacleActor)
	{
		ApplyCrushDamage(ObstacleActor);
	}

	// Tryb Rebound: krata po uderzeniu natychmiast cofa się w górę
	if (BlockBehavior == EGateBlockBehavior::Rebound)
	{
		StartOpening(nullptr);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ReboundTimerHandle,
				this,
				&ADungeonGateProp::HandleReboundTimer,
				ReboundDelay,
				false);
		}
	}
}

void ADungeonGateProp::ApplyCrushDamage(AActor* ObstacleActor)
{
	REQUIRE_AUTHORITY();

	if (CrushDamage <= 0.0f || !ObstacleActor || ObstacleActor == this)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastCrushDamageTime < CrushDamageInterval)
	{
		return;
	}

	LastCrushDamageTime = CurrentTime;

	if (UDamageableComponent* DmgComp = ObstacleActor->FindComponentByClass<UDamageableComponent>())
	{
		if (!DmgComp->IsInvulnerable() && !DmgComp->IsDestroyed())
		{
			UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s Crushing %s for %.1f Kinetic damage"),
				*NetUtils::GetNetRolePrefix(this), *ObstacleActor->GetName(), CrushDamage);

			DmgComp->ApplyDamage(CrushDamage, EDamageType::Kinetic, this);
		}
	}
}

void ADungeonGateProp::OnRep_GateState()
{
	ReceiveGateStateChangedCosmetic(GateState);
	OnGateStateChanged.Broadcast(GateState, nullptr);

	switch (GateState)
	{
	case EGateState::Opening:
	case EGateState::Closing:
		SetActorTickEnabled(true);
		break;

	case EGateState::Open:
		if (GateMeshComponent)
		{
			GateMeshComponent->SetRelativeLocation(DefaultGateRelativeLocation + OpenOffset);
		}
		SetActorTickEnabled(false);
		break;

	case EGateState::Closed:
		if (GateMeshComponent)
		{
			GateMeshComponent->SetRelativeLocation(DefaultGateRelativeLocation);
		}
		SetActorTickEnabled(false);
		OnGateSlamDown.Broadcast();
		ReceiveGateSlamDownCosmetic();
		break;
	}
}

void ADungeonGateProp::HandleAutoCloseTimer()
{
	REQUIRE_AUTHORITY();

	if (GateState == EGateState::Open)
	{
		CloseGate(nullptr);
	}
}

void ADungeonGateProp::HandleReboundTimer()
{
	REQUIRE_AUTHORITY();

	if (GateState == EGateState::Open)
	{
		CloseGate(nullptr);
	}
}

void ADungeonGateProp::HandleOnDestroyed(AActor* DestroyedActor)
{
	UE_LOG(LogDungeonMechanisms, Log, TEXT("[GateProp]%s %s was destroyed!"),
		*NetUtils::GetNetRolePrefix(this), *GetName());

	if (GateMeshComponent)
	{
		GateMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GateMeshComponent->SetVisibility(false);
	}

	SetActorTickEnabled(false);
}

bool ADungeonGateProp::CanReceiveSurfaceCells_Implementation() const
{
	if (IsActorBeingDestroyed())
	{
		return false;
	}

	if (bIsDestructible && DamageableComponent && DamageableComponent->IsDestroyed())
	{
		return false;
	}

	return true;
}

USceneComponent* ADungeonGateProp::GetSurfaceTransformComponent_Implementation() const
{
	return GateMeshComponent;
}


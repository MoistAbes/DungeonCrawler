#include "MechanismTrapBase.h"

#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"

AMechanismTrapBase::AMechanismTrapBase()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(false);

	BaseMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMeshComponent"));
	RootComponent = BaseMeshComponent;

	BaseMeshComponent->SetSimulatePhysics(false);
	BaseMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	BaseMeshComponent->SetGenerateOverlapEvents(false);

	DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));
	StatusEffectComponent = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("StatusEffectComponent"));

	MaterialType = EPhysicalMaterialType::Stone;
	bIsDestructible = false;
	bIsContinuousLoop = false;
	bAutoActivateOnBeginPlay = true;
	LoopInterval = 3.0f;
	InitialDelay = 0.0f;
	bIsTrapActive = false;
}

void AMechanismTrapBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMechanismTrapBase, bIsTrapActive);
}

void AMechanismTrapBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (DamageableComponent && bIsDestructible)
	{
		DamageableComponent->OnDestroyed.AddDynamic(this, &AMechanismTrapBase::HandleOnDestroyed);
	}
}

void AMechanismTrapBase::BeginPlay()
{
	Super::BeginPlay();

	if (!bIsDestructible && DamageableComponent)
	{
		DamageableComponent->SetComponentTickEnabled(false);
	}

	if (HasAuthority() && bIsContinuousLoop && bAutoActivateOnBeginPlay)
	{
		SetTrapActive(true, nullptr);
	}
}

void AMechanismTrapBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		StopLoopTimer();
	}

	Super::EndPlay(EndPlayReason);
}

void AMechanismTrapBase::SetMechanismState_Implementation(bool bActive, AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	if (bIsContinuousLoop)
	{
		// W trybie ciągłej pętli sygnał włącza lub wyłącza periodyczne uderzenia/wystrzały
		SetTrapActive(bActive, TriggeringActor);
	}
	else
	{
		// W trybie wyzwalanym (on-demand), sygnał true powoduje pojedynczy cykl działania pułapki
		if (bActive)
		{
			TriggerTrap(TriggeringActor);
		}
	}
}

void AMechanismTrapBase::SetTrapActive(bool bNewActive, AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	if (bIsTrapActive == bNewActive)
	{
		return;
	}

	bIsTrapActive = bNewActive;

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[MechanismTrap]%s %s active state set to: %s by TriggeringActor: %s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(),
		bIsTrapActive ? TEXT("ACTIVE") : TEXT("INACTIVE"),
		TriggeringActor ? *TriggeringActor->GetName() : TEXT("None"));

	if (bIsTrapActive)
	{
		if (bIsContinuousLoop)
		{
			StartLoopTimer();
		}
	}
	else
	{
		StopLoopTimer();
	}

	ReceiveTrapActiveChangedCosmetic(bIsTrapActive);
	OnTrapActiveChanged.Broadcast(bIsTrapActive);
}

void AMechanismTrapBase::TriggerTrap(AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	UE_LOG(LogDungeonMechanisms, Verbose, TEXT("[MechanismTrap]%s %s triggered action by: %s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(),
		TriggeringActor ? *TriggeringActor->GetName() : TEXT("Timer/Auto"));

	ExecuteTrapAction(TriggeringActor);

	ReceiveTrapTriggeredCosmetic();
	OnTrapTriggered.Broadcast(TriggeringActor);
}

void AMechanismTrapBase::StartLoopTimer()
{
	REQUIRE_AUTHORITY();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimerHandle);
		World->GetTimerManager().SetTimer(
			LoopTimerHandle,
			this,
			&AMechanismTrapBase::HandleLoopTimerTick,
			LoopInterval,
			true,
			InitialDelay);
	}
}

void AMechanismTrapBase::StopLoopTimer()
{
	REQUIRE_AUTHORITY();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimerHandle);
	}
}

void AMechanismTrapBase::HandleLoopTimerTick()
{
	REQUIRE_AUTHORITY();

	if (bIsTrapActive)
	{
		TriggerTrap(nullptr);
	}
}

void AMechanismTrapBase::OnRep_IsTrapActive()
{
	ReceiveTrapActiveChangedCosmetic(bIsTrapActive);
	OnTrapActiveChanged.Broadcast(bIsTrapActive);
}

void AMechanismTrapBase::HandleOnDestroyed(AActor* DestroyedActor)
{
	REQUIRE_AUTHORITY();

	if (!bIsDestructible)
	{
		return;
	}

	UE_LOG(LogDungeonMechanisms, Warning, TEXT("[MechanismTrap]%s %s physically destroyed!"),
		*NetUtils::GetNetRolePrefix(this), *GetName());

	StopLoopTimer();
	Destroy();
}

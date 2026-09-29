#include "SwitchPropBase.h"

#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Interfaces/MechanismReceiverInterface.h"

ASwitchPropBase::ASwitchPropBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// Włączamy replikację sieciową aktora
	bReplicates = true;
	SetReplicateMovement(false); // Przełączniki i płyty są zakotwiczone w podłożu/ścianie

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	MeshComponent->SetGenerateOverlapEvents(false);

	DamageableComponent = CreateDefaultSubobject<UDamageableComponent>(TEXT("DamageableComponent"));
	StatusEffectComponent = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("StatusEffectComponent"));

	MaterialType = EPhysicalMaterialType::Stone;
	bIsDestructible = false;
	bAllowSwitchBack = true;
	bCanBeUsed = true;
	bIsActive = false;
	bHasBeenTriggered = false;
	bInitialActiveState = false;
}

void ASwitchPropBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASwitchPropBase, bIsActive);
	DOREPLIFETIME(ASwitchPropBase, bHasBeenTriggered);
}

void ASwitchPropBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (DamageableComponent && bIsDestructible)
	{
		DamageableComponent->OnDestroyed.AddDynamic(this, &ASwitchPropBase::HandleOnDestroyed);
	}
}

void ASwitchPropBase::BeginPlay()
{
	Super::BeginPlay();

	// Zapamiętujemy stan wyjściowy przełącznika
	bInitialActiveState = bIsActive;

	// Jeśli obiekt nie jest niszczalny, wyłączamy nasłuchiwanie
	if (!bIsDestructible && DamageableComponent)
	{
		DamageableComponent->SetComponentTickEnabled(false);
	}
}

bool ASwitchPropBase::SetActiveState(bool bNewState, AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY_RET(false);

	if (!bCanBeUsed)
	{
		return false;
	}

	// Brak zmiany stanu
	if (bIsActive == bNewState)
	{
		return false;
	}

	// Jeśli to przełącznik jednokierunkowy i został już raz przestawiony ze stanu początkowego,
	// nie pozwalamy na powrót do stanu wyjściowego
	if (!bAllowSwitchBack && bHasBeenTriggered)
	{
		UE_LOG(LogDungeonMechanisms, Log, TEXT("[SwitchProp]%s %s cannot be switched back (bAllowSwitchBack = false, already triggered)"),
			*NetUtils::GetNetRolePrefix(this), *GetName());
		return false;
	}

	bIsActive = bNewState;
	bHasBeenTriggered = true;

	UE_LOG(LogDungeonMechanisms, Log, TEXT("[SwitchProp]%s %s toggled state to: %s by TriggeringActor: %s"),
		*NetUtils::GetNetRolePrefix(this), *GetName(),
		bIsActive ? TEXT("ON") : TEXT("OFF"),
		TriggeringActor ? *TriggeringActor->GetName() : TEXT("None"));

	// 1. Lokalne wywołanie prezentacji na serwerze
	HandleStateChanged(bIsActive, TriggeringActor);

	// 2. Powiadomienie powiązanych mechanizmów w świecie (Tylko Serwer)
	NotifyTargetMechanisms(bIsActive, TriggeringActor);

	// 3. Rozgłoszenie zdarzenia dynamicznego
	OnSwitchToggled.Broadcast(bIsActive, TriggeringActor);

	return true;
}

void ASwitchPropBase::OnRep_IsActive()
{
	HandleStateChanged(bIsActive, nullptr);
	OnSwitchToggled.Broadcast(bIsActive, nullptr);
}

void ASwitchPropBase::HandleStateChanged(bool bNewState, AActor* TriggeringActor)
{
	// Wywołanie hooka blueprintowego dla SFX / VFX / Animacji
	ReceiveStateChangedCosmetic(bNewState);
}

void ASwitchPropBase::NotifyTargetMechanisms(bool bNewState, AActor* TriggeringActor)
{
	REQUIRE_AUTHORITY();

	for (AActor* Target : TargetMechanisms)
	{
		if (IsValid(Target) && Target->GetClass()->ImplementsInterface(UMechanismReceiverInterface::StaticClass()))
		{
			IMechanismReceiverInterface::Execute_SetMechanismState(Target, bNewState, TriggeringActor);
		}
	}
}

void ASwitchPropBase::HandleOnDestroyed(AActor* DestroyedActor)
{
	REQUIRE_AUTHORITY();

	if (!bIsDestructible)
	{
		return;
	}

	UE_LOG(LogDungeonMechanisms, Warning, TEXT("[SwitchProp]%s %s has been physically destroyed!"),
		*NetUtils::GetNetRolePrefix(this), *GetName());

	// W momencie zniszczenia dezaktywujemy przełącznik i niszczymy aktora
	if (bIsActive)
	{
		NotifyTargetMechanisms(false, nullptr);
		OnSwitchToggled.Broadcast(false, nullptr);
	}

	Destroy();
}

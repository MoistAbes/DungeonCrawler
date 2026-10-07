#include "MovementModifierComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h"
#include "MyProject/Environment/Elements/Utilities/ElementalReactionRules.h"

UMovementModifierComponent::UMovementModifierComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.TickInterval = 0.05f;

	SetIsReplicatedByDefault(true);
}

void UMovementModifierComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMovementModifierComponent, EffectiveModifier);
}

void UMovementModifierComponent::CacheBaseValues()
{
	if (bBaseValuesCached)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!CachedMovementComponent.IsValid())
	{
		if (const ACharacter* Character = Cast<ACharacter>(Owner))
		{
			CachedMovementComponent = Character->GetCharacterMovement();
		}
		else
		{
			CachedMovementComponent = Owner->FindComponentByClass<UCharacterMovementComponent>();
		}
	}

	if (UCharacterMovementComponent* CMC = CachedMovementComponent.Get())
	{
		BaseMaxWalkSpeed = CMC->MaxWalkSpeed;
		BaseGroundFriction = CMC->GroundFriction;
		BaseBrakingDecelerationWalking = CMC->BrakingDecelerationWalking;
		BaseBrakingDecelerationFalling = CMC->BrakingDecelerationFalling;
		BaseFallingLateralFriction = CMC->FallingLateralFriction;
		bBaseValuesCached = true;
	}
}

void UMovementModifierComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheBaseValues();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	Owner->OnActorHit.AddDynamic(this, &UMovementModifierComponent::HandleOwnerHit);

	CachedStatusComponent = Owner->FindComponentByClass<UStatusEffectComponent>();
	if (UStatusEffectComponent* StatusComp = CachedStatusComponent.Get())
	{
		StatusComp->OnStatusEffectApplied.AddDynamic(this, &UMovementModifierComponent::HandleStatusApplied);
		StatusComp->OnStatusEffectRemoved.AddDynamic(this, &UMovementModifierComponent::HandleStatusRemoved);
	}

	if (UWorld* World = GetWorld())
	{
		CachedSurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>();
	}

	if (Owner->HasAuthority())
	{
		PrimaryComponentTick.TickInterval = SurfaceCheckInterval;
		RecalculateModifiers();
	}
	else
	{
		// Klienci nie potrzebują ticku - ich CMC jest w 100% synchronizowane replikacją OnRep_EffectiveModifier
		SetComponentTickEnabled(false);
		ApplyEffectiveModifiersToCMC();
	}
}

void UMovementModifierComponent::OnRep_EffectiveModifier()
{
	CacheBaseValues();
	ApplyEffectiveModifiersToCMC();
	OnMovementModifierChanged.Broadcast(EffectiveModifier);
}

void UMovementModifierComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	UCharacterMovementComponent* CMC = CachedMovementComponent.Get();
	if (!CMC || !bBaseValuesCached)
	{
		return;
	}

	// Sprawdzamy powierzchnię tylko jeśli postać się porusza lub zmieniła pozycję o co najmniej 10 cm
	const FVector CurrentLoc = Owner->GetActorLocation();
	const bool bMoved = FVector::DistSquared(CurrentLoc, LastFloorCheckLocation) > 100.0f; // 10 cm ^ 2

	if (bMoved || !CMC->Velocity.IsNearlyZero(1.0f))
	{
		LastFloorCheckLocation = CurrentLoc;
		UpdateSurfaceModifier();
	}
}

void UMovementModifierComponent::HandleStatusApplied(EStatusEffectType Status, float Duration)
{
	UpdateBodyModifierFromStatuses();
	ApplyEffectiveModifiersToCMC();
}

void UMovementModifierComponent::HandleStatusRemoved(EStatusEffectType Status)
{
	UpdateBodyModifierFromStatuses();
	ApplyEffectiveModifiersToCMC();
}

void UMovementModifierComponent::HandleOwnerHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// Natychmiastowa reakcja w klatce zderzenia (np. knockback w ścianę lub sufit)
	UpdateSurfaceModifier();
}

void UMovementModifierComponent::UpdateSurfaceModifier()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UDungeonSurfaceSubsystem* SurfaceSubsystem = CachedSurfaceSubsystem.Get();
	if (!SurfaceSubsystem && GetWorld())
	{
		SurfaceSubsystem = GetWorld()->GetSubsystem<UDungeonSurfaceSubsystem>();
		CachedSurfaceSubsystem = SurfaceSubsystem;
	}

	FMovementModifier NewSurfaceModifier;
	if (SurfaceSubsystem)
	{
		NewSurfaceModifier = SurfaceSubsystem->GetSurfaceMovementModifierForActor(Owner);
	}

	if (NewSurfaceModifier != ActiveSurfaceModifier)
	{
		ActiveSurfaceModifier = NewSurfaceModifier;
		ApplyEffectiveModifiersToCMC();
	}
}

void UMovementModifierComponent::UpdateBodyModifierFromStatuses()
{
	FMovementModifier NewBodyModifier;

	if (UStatusEffectComponent* StatusComp = CachedStatusComponent.Get())
	{
		TArray<EStatusEffectType> ActiveStatuses = StatusComp->GetActiveStatuses();
		for (EStatusEffectType Status : ActiveStatuses)
		{
			const FStatusEffectConfig& Config = UElementalReactionRules::GetEffectConfig(Status);
			if (!Config.BodyMovementModifier.IsIdentity())
			{
				NewBodyModifier.CombineWith(Config.BodyMovementModifier);
			}
		}
	}

	ActiveBodyModifier = NewBodyModifier;
}

void UMovementModifierComponent::ApplyEffectiveModifiersToCMC()
{
	UCharacterMovementComponent* CMC = CachedMovementComponent.Get();
	if (!CMC || !bBaseValuesCached)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		const FMovementModifier NewEffective = FMovementModifier::Combine(ActiveSurfaceModifier, ActiveBodyModifier);

		if (NewEffective != EffectiveModifier)
		{
			EffectiveModifier = NewEffective;
			OnMovementModifierChanged.Broadcast(EffectiveModifier);
		}
	}

	if (EffectiveModifier.bImmobilized)
	{
		CMC->MaxWalkSpeed = 0.0f;
	}
	else
	{
		CMC->MaxWalkSpeed = BaseMaxWalkSpeed * EffectiveModifier.SpeedMultiplier;
	}

	CMC->GroundFriction = BaseGroundFriction * EffectiveModifier.GroundFrictionMultiplier;
	CMC->BrakingDecelerationWalking = BaseBrakingDecelerationWalking * EffectiveModifier.BrakingDecelerationMultiplier;
	CMC->BrakingDecelerationFalling = BaseBrakingDecelerationFalling * EffectiveModifier.BrakingDecelerationMultiplier;
	CMC->FallingLateralFriction = BaseFallingLateralFriction * EffectiveModifier.GroundFrictionMultiplier;
}

void UMovementModifierComponent::RecalculateModifiers()
{
	UpdateBodyModifierFromStatuses();
	UpdateSurfaceModifier();
	ApplyEffectiveModifiersToCMC();
}

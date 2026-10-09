#include "DeathComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Shared/Components/InteractionComponent/InteractionComponent.h"
#include "MyProject/Shared/Components/PhysicsCarryComponent/PhysicsCarryComponent.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"

UDeathComponent::UDeathComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}

void UDeathComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UDeathComponent, bIsDead);
	DOREPLIFETIME(UDeathComponent, DeathCause);
}

void UDeathComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		if (UDamageableComponent* Damageable = Owner->FindComponentByClass<UDamageableComponent>())
		{
			Damageable->OnDestroyed.AddDynamic(this, &UDeathComponent::HandleOwnerDestroyed);
		}
	}
}

void UDeathComponent::HandleOwnerDestroyed(AActor* DestroyedActor)
{
	REQUIRE_AUTHORITY();

	if (bIsDead)
	{
		return;
	}

	DeathCause = EvaluateDeathCause();
	bIsDead = true;

	ExecuteDeathSequence();
}

void UDeathComponent::Kill(EDeathCause InCause)
{
	REQUIRE_AUTHORITY();

	if (bIsDead)
	{
		return;
	}

	DeathCause = InCause;
	bIsDead = true;

	ExecuteDeathSequence();
}

void UDeathComponent::OnRep_IsDead()
{
	if (bIsDead)
	{
		ExecuteDeathSequence();
	}
}

EDeathCause UDeathComponent::EvaluateDeathCause() const
{
	if (AActor* Owner = GetOwner())
	{
		if (const UStatusEffectComponent* StatusComp = Owner->FindComponentByClass<UStatusEffectComponent>())
		{
			if (StatusComp->HasStatus(EStatusEffectType::Burning))
			{
				return EDeathCause::Burning;
			}
		}
	}
	return EDeathCause::Default;
}

void UDeathComponent::ExecuteDeathSequence()
{
	UE_LOG(LogDungeonPhysics, Warning, TEXT("[DeathComponent]%s Entity '%s' has entered death state. Cause: %d"),
		*NetUtils::GetNetRolePrefix(this),
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
		static_cast<int32>(DeathCause));

	// 1. Zrzucenie niesionego propa (tylko na serwerze)
	if (NetUtils::HasAuthority(this))
	{
		ReleaseCarriedProps();
	}

	// 2. Odcięcie wejścia i kontroli ruchu
	DisableCharacterControls();

	// 3. Włączenie ragdolla i wyłączenie kolizji kapsuły
	EnableRagdollPhysics();

	// 4. Efekty kosmetyczne i rozgłoszenie zdarzenia
	ReceiveDeathCosmetics(DeathCause);
	OnDeath.Broadcast(GetOwner(), DeathCause);
}

void UDeathComponent::DisableCharacterControls()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 1. Zablokowanie wejścia ruchu na PlayerControllerze (natywny mechanizm UE - blokuje WASD, zachowuje Look i Zoom)
	if (APawn* Pawn = Cast<APawn>(Owner))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->SetIgnoreMoveInput(true);
		}
	}

	// 2. Zatrzymanie ruchu i wyłączenie CMC (postać nie może chodzić ani skakać)
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
			MoveComp->DisableMovement();
			MoveComp->SetComponentTickEnabled(false);
		}
	}

	// 3. Dezaktywacja komponentów interakcji i noszenia obiektów
	if (UActorComponent* InterComp = Owner->FindComponentByClass<UInteractionComponent>())
	{
		InterComp->Deactivate();
	}

	if (UActorComponent* CarryComp = Owner->FindComponentByClass<UPhysicsCarryComponent>())
	{
		CarryComp->Deactivate();
	}
}

void UDeathComponent::ReleaseCarriedProps()
{
	if (AActor* Owner = GetOwner())
	{
		if (UPhysicsCarryComponent* CarryComp = Owner->FindComponentByClass<UPhysicsCarryComponent>())
		{
			if (CarryComp->IsCarrying())
			{
				CarryComp->DropOrSwing();
			}
		}
	}
}

void UDeathComponent::EnableRagdollPhysics()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	USkeletalMeshComponent* SkeletalMesh = Character->GetMesh();

	// Sprawdzamy, czy postać posiada pełnoprawny model szkieletowy z Physics Assetem (przyszłościowy humanoid)
	const bool bHasSkeletalPhysics = (SkeletalMesh != nullptr)
		&& (SkeletalMesh->GetSkeletalMeshAsset() != nullptr)
		&& (SkeletalMesh->GetPhysicsAsset() != nullptr);

	if (bHasSkeletalPhysics)
	{
		// Wariant A: Pełny szkieletowy ragdoll (wiotkie ciało, stawy)
		if (Capsule)
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		}

		SkeletalMesh->SetCollisionProfileName(RagdollCollisionProfile);
		SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SkeletalMesh->SetSimulatePhysics(true);
		SkeletalMesh->SetAllBodiesSimulatePhysics(true);
		SkeletalMesh->bBlendPhysics = true;
		SkeletalMesh->WakeAllRigidBodies();

		if (bApplyDeathImpulse)
		{
			const FVector Impulse = (-Character->GetActorForwardVector() * 500.0f) + FVector(0.0f, 0.0f, 250.0f);
			SkeletalMesh->AddImpulse(Impulse, NAME_None, true);
		}
	}
	else
	{
		// Wariant B: Kapsuła / Cylinder (obecny fizyczny prototyp postaci)
		// Przekształcamy kinematyczną kapsułę w 100% symulowaną fizyczną bryłę Chaos!
		if (Capsule)
		{
			Capsule->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

			// Odblokowujemy obrót we wszystkich osiach, by cylinder bezwładnie runął na posadzkę
			Capsule->BodyInstance.bLockXRotation = false;
			Capsule->BodyInstance.bLockYRotation = false;
			Capsule->BodyInstance.bLockZRotation = false;

			Capsule->SetSimulatePhysics(true);
			Capsule->WakeRigidBody();

			if (bApplyDeathImpulse)
			{
				// Impuls liniowy (odrzut do tyłu i lekko w górę)
				const FVector LinearImpulse = (-Character->GetActorForwardVector() * 350.0f) + FVector(0.0f, 0.0f, 150.0f);
				Capsule->AddImpulse(LinearImpulse, NAME_None, true);

				// Impuls kątowy (moment obrotowy), by natychmiast przewrócić cylinder na bok
				const FVector AngularImpulse = (Character->GetActorRightVector() * 300.0f) + FVector(0.0f, 0.0f, 100.0f);
				Capsule->AddAngularImpulseInDegrees(AngularImpulse, NAME_None, true);
			}
		}

		// Dodatkowo: jeśli postać posiada podpięte StaticMeshe, włączamy na nich fizykę
		TArray<UStaticMeshComponent*> StaticMeshes;
		Character->GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* SM : StaticMeshes)
		{
			if (SM && !SM->IsSimulatingPhysics())
			{
				SM->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
				SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				SM->SetSimulatePhysics(true);
				SM->WakeRigidBody();
			}
		}
	}
}

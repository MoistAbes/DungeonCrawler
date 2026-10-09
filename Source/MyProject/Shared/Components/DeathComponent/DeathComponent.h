#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeathComponent.generated.h"

class UDamageableComponent;
class UCharacterMovementComponent;
class UPhysicsCarryComponent;
class UInteractionComponent;
class UStatusEffectComponent;

/**
 * Przyczyna zgonu postaci (dla celów telemetrycznych, animacji i efektów VFX/SFX).
 */
UENUM(BlueprintType)
enum class EDeathCause : uint8
{
	Default     UMETA(DisplayName = "Default / Physical"),
	Burning     UMETA(DisplayName = "Burning (Fire)"),
	Crushed     UMETA(DisplayName = "Crushed (Trap / Gate)"),
	Elemental   UMETA(DisplayName = "Elemental Reaction")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDeathSignature, AActor*, DeadActor, EDeathCause, Cause);

/**
 * Uniwersalny komponent śmierci i przejścia w stan bezwładny (Ragdoll) dla istot żywych.
 * 
 * Odpowiedzialność (SRP):
 * - Nasłuchuje UDamageableComponent::OnDestroyed (CurrentDurability <= 0).
 * - Autorytatywnie zarządza stanem śmierci postaci (Server-Authoritative).
 * - Odcina wejście i sterowanie gracza (APawn::DisableInput).
 * - Wyłącza komponent ruchu (CMC::DisableMovement, StopMovementImmediately).
 * - Zwalnia trzymane rekwizyty fizyczne (UPhysicsCarryComponent::DropOrSwing).
 * - Dezaktywuje możliwość interakcji z mechanizmami lochu (UInteractionComponent).
 * - Wyłącza kolizję kapsuły i aktywuje fizyczny ragdoll (USkeletalMeshComponent / UPrimitiveComponent).
 * - Replikuje stan bIsDead oraz DeathCause do klientów z zerowym narzutem pasma w spoczynku (Zero-Bandwidth).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UDeathComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeathComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Czy postać jest martwa */
	UFUNCTION(BlueprintPure, Category = "Custom|Death")
	bool IsDead() const { return bIsDead; }

	/** Zwraca ustaloną przyczynę zgonu */
	UFUNCTION(BlueprintPure, Category = "Custom|Death")
	EDeathCause GetDeathCause() const { return DeathCause; }

	/**
	 * Autorytatywne wymuszenie śmierci postaci (np. przez kill volume, pułapkę lub komendę).
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|Death")
	void Kill(EDeathCause InCause = EDeathCause::Default);

	/** Zdarzenie śmierci (rozgłaszane na serwerze i u klientów) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Death")
	FOnDeathSignature OnDeath;

	/** Zwraca domyślny profil kolizji ragdolla */
	UFUNCTION(BlueprintPure, Category = "Custom|Death")
	FName GetRagdollCollisionProfile() const { return RagdollCollisionProfile; }

protected:
	virtual void BeginPlay() override;

	/** Obsługa zdarzenia OnDestroyed z UDamageableComponent */
	UFUNCTION()
	void HandleOwnerDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnRep_IsDead();

	/** Główna sekwencja przejścia w stan śmierci */
	void ExecuteDeathSequence();

	/** Odłączenie kontroli, wygaszenie ruchu i interakcji */
	void DisableCharacterControls();

	/** Zrzucenie niesionego propa */
	void ReleaseCarriedProps();

	/** Aktywacja fizyki ragdolla na siatce i wyłączenie kolizji kapsuły */
	void EnableRagdollPhysics();

	/** Ewaluacja przyczyny śmierci na podstawie aktywnych statusów */
	EDeathCause EvaluateDeathCause() const;

	/** Blueprintowy hook dla efektów kosmetycznych śmierci (dźwięki, VFX, spalenizna) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Death", meta = (DisplayName = "OnDeathCosmetics"))
	void ReceiveDeathCosmetics(EDeathCause Cause);

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Czy po wejściu w stan śmierci nadać impuls uderzeniowy (odrzut / przewrócenie) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Death")
	bool bApplyDeathImpulse = true;

	/** Domyślny profil kolizji używany przy wejściu w stan ragdolla */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Death")
	FName RagdollCollisionProfile = TEXT("Ragdoll");

private:
	/** Replikowana flaga śmierci */
	UPROPERTY(ReplicatedUsing = OnRep_IsDead, VisibleInstanceOnly, Category = "Custom|Death")
	bool bIsDead = false;

	/** Replikowana przyczyna śmierci */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Custom|Death")
	EDeathCause DeathCause = EDeathCause::Default;
};

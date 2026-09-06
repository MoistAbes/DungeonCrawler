#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "SwitchPropBase.generated.h"

class UStaticMeshComponent;
class UDamageableComponent;
class UStatusEffectComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSwitchToggledSignature, bool, bNewState, AActor*, TriggeringActor);

/**
 * Bazowa klasa domenowa dla wszystkich aktywatorów i przełączników mechanizmów w lochu
 * (dźwignie, płyty naciskowe, linki potykaczy, czujniki).
 * 
 * Zapewnia:
 * - Autorytatywną kontrolę stanu sieciowego (Server-Authoritative Co-op).
 * - Obsługę przełączników jednokierunkowych / zatrzaskowych (bAllowSwitchBack).
 * - Bezpośrednie dispatchowanie sygnału do powiązanych aktorów docelowych (TargetMechanisms).
 * - Implementację Świętej Trójcy: tożsamość materiałowa, punkty życia/odporność, reakcje żywiołowe.
 */
UCLASS(Abstract)
class MYPROJECT_API ASwitchPropBase : public AActor, public IMaterialProviderInterface
{
	GENERATED_BODY()

public:
	ASwitchPropBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- IMaterialProviderInterface ---
	virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

	// --- Stan i Sterowanie ---

	/** Zwraca bieżący stan logiczny przełącznika (true = włączony / aktywny, false = wyłączony) */
	UFUNCTION(BlueprintPure, Category = "Custom|Switch")
	bool IsActive() const { return bIsActive; }

	/** Sprawdza, czy przełącznik może być aktualnie użyty (nie jest zablokowany biznesowo) */
	UFUNCTION(BlueprintPure, Category = "Custom|Switch")
	bool CanBeUsed() const { return bCanBeUsed; }

	/** Zmienia możliwość użycia przełącznika (np. odblokowanie po znalezieniu klucza lub włączeniu zasilania) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Switch")
	void SetCanBeUsed(bool bInCanBeUsed) { bCanBeUsed = bInCanBeUsed; }

	/** Czy przełącznik pozwala na powrót do stanu wyjściowego po pierwszej zmianie */
	UFUNCTION(BlueprintPure, Category = "Custom|Switch")
	bool AllowsSwitchBack() const { return bAllowSwitchBack; }

	/** Czy przełącznik został już jednorazowo przestawiony */
	UFUNCTION(BlueprintPure, Category = "Custom|Switch")
	bool HasBeenTriggered() const { return bHasBeenTriggered; }

	/**
	 * Autorytatywnie zmienia stan logiczny przełącznika (wywoływane wyłącznie na serwerze).
	 * Rozsyła sygnały do TargetMechanisms oraz rozgłasza zdarzenie OnSwitchToggled.
	 * 
	 * @param bNewState Docelowy stan (true = ON, false = OFF)
	 * @param TriggeringActor Aktor odpowiedzialny za aktywację
	 * @return true jeśli stan został skutecznie zmieniony
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|Switch")
	virtual bool SetActiveState(bool bNewState, AActor* TriggeringActor);

	// --- Gettery Komponentów ---

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStatusEffectComponent* GetStatusEffectComponent() const { return StatusEffectComponent; }

	// --- Zdarzenia ---

	/** Wywoływane przy każdej zmianie stanu przełącznika (zwraca nowy stan oraz aktora aktywującego) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnSwitchToggledSignature OnSwitchToggled;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/**
	 * Metoda rozszerzenia dla klas pochodnych i Blueprintów przy zmianie stanu (SFX, animacje).
	 * Wywoływana zarówno na serwerze, jak i u klientów przez OnRep_IsActive.
	 */
	virtual void HandleStateChanged(bool bNewState, AActor* TriggeringActor);

	/** Blueprintowy hook do kosmetycznych efektów (dźwięki, cząsteczki, animacja wajchy) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Custom|Switch", meta = (DisplayName = "OnStateChangedCosmetic"))
	void ReceiveStateChangedCosmetic(bool bNewState);

	/** Powiadamia wszystkich aktorów z TargetMechanisms implementujących IMechanismReceiverInterface */
	virtual void NotifyTargetMechanisms(bool bNewState, AActor* TriggeringActor);

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Główna siatka statyczna reprezentująca przełącznik / obudowę */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Komponent punktów wytrzymałości i zniszczenia */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDamageableComponent> DamageableComponent;

	/** Komponent obsługujący stany żywiołowe (np. przewodzenie prądu) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStatusEffectComponent> StatusEffectComponent;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Tożsamość materiałowa przełącznika (np. Stone, Metal, Wood) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Stone;

	/** Czy przełącznik może zostać zniszczony fizycznie */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Destruction")
	bool bIsDestructible = false;

	/** 
	 * Jeśli false, przełącznik po jednokrotnej zmianie stanu ze stanu początkowego
	 * nie może zostać przestawiony z powrotem (One-Way / Single-Use Switch).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Switch")
	bool bAllowSwitchBack = true;

	/** Flaga blokady biznesowej (np. brak klucza, brak zasilania) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom|Switch")
	bool bCanBeUsed = true;

	/** 
	 * Tablica aktorów na scenie, którzy zostaną powiadomieni przez IMechanismReceiverInterface
	 * w momencie włączenia lub wyłączenia przełącznika (np. wrota, kraty, tłoki).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Custom|Switch")
	TArray<TObjectPtr<AActor>> TargetMechanisms;

	// -------------------------------------------------------------------------
	// Replikacja Stanu
	// -------------------------------------------------------------------------

	/** Aktualny stan logiczny zreplikowany z serwera */
	UPROPERTY(ReplicatedUsing = OnRep_IsActive, EditInstanceOnly, BlueprintReadOnly, Category = "Custom|Switch")
	bool bIsActive = false;

	/** Flaga określająca, czy przełącznik został już przestawiony ze swojego stanu początkowego */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Switch")
	bool bHasBeenTriggered = false;

	/** Początkowy stan przełącznika zarejestrowany na starcie poziomu */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Switch")
	bool bInitialActiveState = false;

	UFUNCTION()
	virtual void OnRep_IsActive();

	UFUNCTION()
	virtual void HandleOnDestroyed(AActor* DestroyedActor);
};

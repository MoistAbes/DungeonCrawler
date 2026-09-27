#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/IGrabbableInterface.h"
#include "MyProject/Shared/Interfaces/IInteractableInterface.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "InteractivePropBase.generated.h"

class UStaticMeshComponent;
class UDamageableComponent;
class UStatusEffectComponent;

/**
 * Bazowa klasa dla interaktywnych elementów wyposażenia lochu (skrzynie, wazy, beczki).
 * Symuluje fizykę Chaos, wspiera chwytanie (IGrabbable), interakcję (IInteractable),
 * tożsamość materiałową (IMaterialProviderInterface) oraz statusy żywiołowe (UStatusEffectComponent).
 * W pełni zoptymalizowana pod kątem kooperacji 1–6 graczy (Server-Authoritative, kwantyzacja transformu).
 */
UCLASS(Abstract)
class MYPROJECT_API AInteractivePropBase : public AActor, 
                                          public IInteractableInterface, 
                                          public IGrabbableInterface,
                                          public IMaterialProviderInterface
{
    GENERATED_BODY()

public:
    AInteractivePropBase();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- IMaterialProviderInterface ---
    virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

    // --- Gettery Komponentów ---

    /** Zwraca główny komponent fizycznej siatki statycznej propa */
    UFUNCTION(BlueprintPure, Category = "Custom|Components")
    UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

    /** Zwraca komponent zarządzający punktami wytrzymałości i zniszczeniem */
    UFUNCTION(BlueprintPure, Category = "Custom|Components")
    UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

    /** Zwraca komponent zarządzający stanami żywiołowymi (np. podpalenie) */
    UFUNCTION(BlueprintPure, Category = "Custom|Components")
    UStatusEffectComponent* GetStatusEffectComponent() const { return StatusEffectComponent; }

    // --- IInteractableInterface ---
    virtual void Interact(AActor* Interactor) override;
    virtual bool CanInteract(const AActor* Interactor) const override;

    // --- IGrabbableInterface ---
    virtual bool CanGrab(const AActor* Grabber) const override;
    virtual void OnGrabbed(AActor* Grabber) override;
    virtual void OnDropped(AActor* Dropper, const FVector& LaunchVelocity = FVector::ZeroVector) override;
    virtual float GetMass() const override;
    virtual bool IsGrabbed() const override { return CarryingActor != nullptr; }

    virtual void Tick(float DeltaTime) override;

protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;

    /** Główna siatka statyczna propa symulująca fizykę Chaos */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    /** Komponent wytrzymałości fizycznej i destrukcji obiektu */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UDamageableComponent> DamageableComponent;

    /** Komponent obsługujący reakcje żywiołowe i efekty statusów */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
    TObjectPtr<UStatusEffectComponent> StatusEffectComponent;

    /** Tożsamość materiałowa propa determinująca reakcje chemiczne (np. Wood podatne na ogień, Stone odporne) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
    EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Wood;

    /** Czy gracz lub postać może chwycić i podnieść ten obiekt do rąk */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Interaction")
    bool bCanBeGrabbed = true;

    /** Wskaźnik na postać aktualnie niosącą ten rekwizyt (Replikowany do wszystkich klientów) */
    UPROPERTY(ReplicatedUsing = OnRep_CarryingActor, VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|State")
    TObjectPtr<AActor> CarryingActor = nullptr;

    /** Replikowany wektor prędkości rzutu zapewniający natychmiastowy lot na maszynach klientów */
    UPROPERTY(Replicated)
    FVector_NetQuantize RepLaunchVelocity = FVector_NetQuantize::ZeroVector;

    UFUNCTION()
    virtual void OnRep_CarryingActor();

    UFUNCTION()
    virtual void HandleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                   const FHitResult& Hit);

    UFUNCTION()
    virtual void HandleComponentWake(UPrimitiveComponent* WakingComponent, FName BoneName);

    UFUNCTION()
    virtual void HandleComponentSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

    UFUNCTION()
    virtual void HandleOnDestroyed(AActor* DestroyedActor);

private:
    /** Poprzedni trzymający aktor (wykorzystywany lokalnie przez OnRep_CarryingActor do czystego odpięcia) */
    UPROPERTY()
    TObjectPtr<AActor> LastCarryingActor = nullptr;
};

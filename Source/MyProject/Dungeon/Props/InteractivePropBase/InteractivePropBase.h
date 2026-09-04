#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
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

    // --- IMaterialProviderInterface ---
    virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

    // --- Gettery Komponentów ---
    UFUNCTION(BlueprintPure, Category = "Dungeon|Prop")
    UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

    UFUNCTION(BlueprintPure, Category = "Dungeon|Prop")
    UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

    UFUNCTION(BlueprintPure, Category = "Dungeon|Prop")
    UStatusEffectComponent* GetStatusEffectComponent() const { return StatusEffectComponent; }

    // --- IInteractableInterface ---
    virtual void Interact(AActor* Interactor) override;
    virtual bool CanInteract(const AActor* Interactor) const override;

    // --- IGrabbableInterface ---
    virtual bool CanGrab(const AActor* Grabber) const override;
    virtual void OnGrabbed(AActor* Grabber) override;
    virtual void OnDropped(AActor* Dropper) override;
    virtual float GetMass() const override;
    virtual bool IsGrabbed() const override { return bIsBeingCarried; }

protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDamageableComponent> DamageableComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStatusEffectComponent> StatusEffectComponent;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Material")
    EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Wood;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Config")
    bool bCanBeGrabbed = true;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Prop|State")
    bool bIsBeingCarried = false;

    UFUNCTION()
    virtual void HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                   const FHitResult& Hit);

    UFUNCTION()
    virtual void HandleOnDestroyed(AActor* DestroyedActor);
};

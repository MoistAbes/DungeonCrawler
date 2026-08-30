#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Combat/Components/DamagableComponent/DamageableComponent.h"
#include "MyProject/Interaction/Interfaces/IGrabbableInterface/IGrabbableInterface.h"
#include "MyProject/Interaction/Interfaces/InteractableInterface/IInteractableInterface.h"
#include "InteractivePropBase.generated.h"

class UStaticMeshComponent;

UCLASS(Abstract)
class MYPROJECT_API AInteractivePropBase : public AActor, 
                                          public IInteractableInterface, 
                                          public IGrabbableInterface
{
    GENERATED_BODY()

public:
    AInteractivePropBase();

    // --- IInteractableInterface ---
    virtual void Interact(AActor* Interactor) override;
    virtual bool CanInteract(const AActor* Interactor) const override;

    // --- IGrabbableInterface ---
    virtual bool CanGrab(const AActor* Grabber) const override;
    virtual void OnGrabbed(AActor* Grabber) override;
    virtual void OnDropped(AActor* Dropper) override;
    virtual float GetMass() const override;

protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDamageableComponent> DamageableComponent;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Config")
    bool bCanBeGrabbed = true;

    UFUNCTION()
    virtual void HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                   const FHitResult& Hit);

    UFUNCTION()
    virtual void HandleOnDestroyed(AActor* DestroyedActor);
};
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../IInteractableInterface.h"
#include "InteractivePropBase.generated.h"

class UStaticMeshComponent;

/**
 * Domenowa encja bazowa dla fizycznych obiektów w świecie gry.
 * Implementuje kontrakt IInteractableInterface.
 */
UCLASS(Abstract)
class MYPROJECT_API AInteractivePropBase : public AActor, public IInteractableInterface
{
    GENERATED_BODY()

public:
    AInteractivePropBase();

    // --- IInteractableInterface Implementation ---
    virtual void Interact(AActor* Interactor) override;
    virtual bool CanInteract(const AActor* Interactor) const override;
    virtual void OnGrabbed(AActor* Grabber) override;
    virtual void OnDropped(AActor* Dropper) override;
    virtual float GetMass() const override;
    virtual EPropMaterialType GetMaterialType() const override { return MaterialType; }

protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;

    /** Główny komponent siatki i fizyki */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    // --- Konfiguracja Domenowa ---

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Config")
    EPropMaterialType MaterialType = EPropMaterialType::Wood;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Config", meta = (ClampMin = "0.0"))
    float Durability = 100.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Config", meta = (ClampMin = "0.0"))
    float DamageImpactThreshold = 5000.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prop|Config")
    bool bCanBeGrabbed = true;

    // --- Reakcje i Logika Biznesowa ---

    UFUNCTION()
    virtual void HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                   const FHitResult& Hit);

    virtual void BreakProp();

private:
    float CurrentDurability = 100.0f;
};
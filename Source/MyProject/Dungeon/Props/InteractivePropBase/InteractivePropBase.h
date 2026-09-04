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
    virtual void OnDropped(AActor* Dropper) override;
    virtual float GetMass() const override;
    virtual bool IsGrabbed() const override { return bIsBeingCarried; }

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

    /** Flaga określająca, czy obiekt jest aktualnie trzymany w rękach przez postać */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|State")
    bool bIsBeingCarried = false;

    // --- Konfiguracja kinetyczna uderzeń (Kinetic Impact Transfer) ---

    /** Czy ten prop przekazuje odrzut (Knockback) i obrażenia uderzanym postaciom, gdy leci z dużą prędkością */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic")
    bool bTransferKineticKnockback = true;

    /** Minimalna prędkość lotu propa w stronę celu (cm/s), aby wywołać odrzut postaci. Zapobiega odrzutom przy zwykłym ocieraniu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "50.0"))
    float MinImpactSpeedForKnockback = 300.0f;

    /** Mnożnik siły odrzutu przekazywanego uderzonemu celowi (skalowany dodatkowo masą propa) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Kinetic", meta = (ClampMin = "0.1"))
    float KnockbackStrengthMultiplier = 1.0f;

    UFUNCTION()
    virtual void HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                   const FHitResult& Hit);

    UFUNCTION()
    virtual void HandleOnDestroyed(AActor* DestroyedActor);
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "DungeonStructureBase.generated.h"

class UStaticMeshComponent;
class UDamageableComponent;
class UStatusEffectComponent;

/**
 * Bazowy aktor dla modularnych elementów architektury lochu (ściany, podłogi, sufity, filary).
 * Domyślnie jest stabilnym elementem statycznym (bez fizyki), ale może być zniszczalny.
 * Posiada UDamageableComponent oraz UStatusEffectComponent do reakcji na żywioły i DoT.
 */
UCLASS(Abstract)
class MYPROJECT_API ADungeonStructureBase : public AActor, public IMaterialProviderInterface
{
	GENERATED_BODY()

public:
	ADungeonStructureBase();

	// --- IMaterialProviderInterface ---
	virtual EPhysicalMaterialType GetMaterialType_Implementation() const override { return MaterialType; }

	// --- Gettery ---

	/** Czy ta struktura może ulec zniszczeniu pod wpływem obrażeń kinetycznych lub żywiołowych */
	UFUNCTION(BlueprintPure, Category = "Custom|Structure")
	bool IsDestructible() const { return bIsDestructible; }

	/** Zwraca komponent siatki architektonicznej struktury */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStaticMeshComponent* GetMeshComponent() const { return StructureMesh; }

	/** Zwraca komponent punktów wytrzymałości struktury */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

	/** Zwraca komponent obsługujący statusy żywiołowe na strukturze */
	UFUNCTION(BlueprintPure, Category = "Custom|Components")
	UStatusEffectComponent* GetStatusEffectComponent() const { return StatusEffectComponent; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Główna siatka statyczna elementu lochu (ściana, podłoga, filar) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStaticMeshComponent> StructureMesh;

	/** Komponent wytrzymałości fizycznej ściany */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDamageableComponent> DamageableComponent;

	/** Komponent obsługujący stany żywiołowe struktury (np. podpalenie drewnianej ściany) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UStatusEffectComponent> StatusEffectComponent;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Tożsamość materiałowa struktury (np. Wood, Stone, Metal) determinująca podatność na żywioły */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Stone;

	/** Flaga określająca, czy ściana/struktura może zostać zniszczona */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Destruction")
	bool bIsDestructible = false;

	/** Współczynnik zachowania pędu przy przebiciu zniszczonej ściany (0.0 = pełne zatrzymanie, 0.6 = zachowuje 60% prędkości) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Destruction", meta = (EditCondition = "bIsDestructible", ClampMin = "0.0", ClampMax = "1.0"))
	float PunchThroughVelocityRetention = 0.6f;

	/** Opcjonalny aktor gruzu/efektu VFX spawnujący się w momencie zniszczenia */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Destruction", meta = (EditCondition = "bIsDestructible"))
	TSubclassOf<AActor> DestroyedDebrisClass;

	// -------------------------------------------------------------------------
	// Reakcja na zderzenia i zniszczenie
	// -------------------------------------------------------------------------

	UFUNCTION()
	virtual void HandleComponentHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UFUNCTION()
	virtual void HandleOnDestroyed(AActor* DestroyedActor);
};

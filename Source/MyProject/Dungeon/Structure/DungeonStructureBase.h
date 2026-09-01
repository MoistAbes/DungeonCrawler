#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Combat/Enums/CombatEnums.h"
#include "MyProject/Shared/Interfaces/MaterialProviderInterface.h"
#include "DungeonStructureBase.generated.h"

class UStaticMeshComponent;
class UDamageableComponent;

/**
 * Bazowy aktor dla modularnych elementów architektury lochu (ściany, podłogi, sufity, filary).
 * Domyślnie jest stabilnym elementem statycznym (bez fizyki), ale może być zniszczalny.
 */
UCLASS(Abstract)
class MYPROJECT_API ADungeonStructureBase : public AActor, public IMaterialProviderInterface
{
	GENERATED_BODY()

public:
	ADungeonStructureBase();

	// --- IMaterialProviderInterface ---
	virtual EPhysicalMaterialType GetMaterialType_Implementation() const override;

	// --- Gettery ---
	UFUNCTION(BlueprintPure, Category = "Dungeon|Structure")
	bool IsDestructible() const { return bIsDestructible; }

	UFUNCTION(BlueprintPure, Category = "Dungeon|Structure")
	UStaticMeshComponent* GetMeshComponent() const { return StructureMesh; }

	UFUNCTION(BlueprintPure, Category = "Dungeon|Structure")
	UDamageableComponent* GetDamageableComponent() const { return DamageableComponent; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StructureMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDamageableComponent> DamageableComponent;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Structure|Material")
	EPhysicalMaterialType MaterialType = EPhysicalMaterialType::Stone;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Structure|Destruction")
	bool bIsDestructible = false;

	/** Współczynnik zachowania pędu przy przebiciu zniszczonej ściany (0.0 = pełne zatrzymanie, 0.6 = zachowuje 60% prędkości) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Structure|Destruction", meta = (EditCondition = "bIsDestructible", ClampMin = "0.0", ClampMax = "1.0"))
	float PunchThroughVelocityRetention = 0.6f;

	/** Opcjonalny aktor gruzu/efektu VFX spawnujący się w momencie zniszczenia */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Structure|Destruction", meta = (EditCondition = "bIsDestructible"))
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

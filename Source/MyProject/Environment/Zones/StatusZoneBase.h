#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Environment/Zones/Data/ZoneTypes.h"
#include "StatusZoneBase.generated.h"

class USphereComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnZoneReactionSignature, EStatusEffectType, OldStatus, EStatusEffectType, NewStatus);

/**
 * Abstrakcyjna klasa bazowa dla wszystkich stref statusu w lochu.
 * Odpowiada za:
 * - Serwerowy cykl życia i timer (ServerEndTime, Auto-Destroy)
 * - Niezawodny, bezstanowy interwał sprawdzania obecności (0.25s)
 * - Replikację parametrów efektu (Zero-Bandwidth / NetUpdate)
 * - Aplikację statusów żywiołowych (UStatusEffectComponent) i obrażeń ciągłych (UDamageableComponent)
 * - Silnik chemiczny reakcji żywiołowych (UElementalChemistryLibrary)
 */
UCLASS(Abstract)
class MYPROJECT_API AStatusZoneBase : public AActor
{
	GENERATED_BODY()

public:
	AStatusZoneBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

	/** Inicjalizuje podstawowe parametry strefy na serwerze */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	virtual void InitializeZoneBase(
		const FZoneEffectConfig& InConfig,
		float InRadius,
		float InDuration,
		EZoneShapeType InShapeType,
		AActor* InInstigator = nullptr);

	/** Obsługuje uderzenie żywiołem w strefę (np. ogień w plamę oleju) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	virtual void ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage = 0.0f, AActor* HitInstigator = nullptr);

	/** Scala strefę z nowo nałożonym tym samym żywiołem (odświeża czas, opcjonalnie powiększa promień) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	virtual void MergeWithZone(float InDuration, float RadiusGrowthMultiplier = 1.20f, float MaxRadiusCap = 1000.0f);

	/** Zwraca konfigurację efektów strefy */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	const FZoneEffectConfig& GetEffectConfig() const { return EffectConfig; }

	/** Zwraca aktywny typ statusu */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	EStatusEffectType GetStatusType() const { return EffectConfig.AppliedStatus; }

	/** Zwraca promień strefy */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	float GetRadius() const { return Radius; }

	/** Zwraca typ kształtu strefy */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	EZoneShapeType GetShapeType() const { return ShapeType; }

	/** Zwraca czas utworzenia strefy na serwerze */
	UFUNCTION(BlueprintPure, Category = "Custom|Zone")
	float GetZoneCreationTime() const { return ZoneCreationTime; }

	/** Zdarzenie wywoływane przy zachodzeniu reakcji żywiołowej w strefie */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnZoneReactionSignature OnZoneReaction;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Korzeń kolizji strefy (SphereComponent) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<USphereComponent> ZoneCollision;

	// -------------------------------------------------------------------------
	// Parametry i Debug
	// -------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Debug")
	bool bDrawDebugZone = true;

	/** Interwał sprawdzania obecności i przetwarzania efektów strefy (w sekundach) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Zone", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ZoneTickInterval = 0.25f;

	// -------------------------------------------------------------------------
	// Replikacja sieciowa
	// -------------------------------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_EffectConfig)
	FZoneEffectConfig EffectConfig;

	UPROPERTY(ReplicatedUsing = OnRep_Radius)
	float Radius = 300.0f;

	UPROPERTY(ReplicatedUsing = OnRep_ServerEndTime)
	float ServerEndTime = 0.0f;

	UPROPERTY(Replicated)
	float ZoneCreationTime = 0.0f;

	UPROPERTY(Replicated)
	EZoneShapeType ShapeType = EZoneShapeType::SurfaceSplash;

	UFUNCTION()
	virtual void OnRep_EffectConfig();

	UFUNCTION()
	virtual void OnRep_Radius();

	UFUNCTION()
	virtual void OnRep_ServerEndTime();

	// -------------------------------------------------------------------------
	// Zdarzenia kolizji i logika strefy
	// -------------------------------------------------------------------------

	UFUNCTION()
	virtual void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	virtual void ProcessActiveOverlaps();
	virtual bool IsActorEligibleForZoneEffect(AActor* TargetActor, UPrimitiveComponent* TargetComp) const;
	virtual bool IsOverruledByNewerLiquidZone(const FVector& TargetLocation) const;

	// -------------------------------------------------------------------------
	// Interfejs polimorficzny (implementowany przez klasy potomne)
	// -------------------------------------------------------------------------

	/** Sprawdza, czy bryła kolizji celu znajduje się wewnątrz specyficznej geometrii strefy */
	virtual bool IsActorWithinZoneGeometry(const FBoxSphereBounds& Bounds) const { return false; }

	/** Rysuje debugowe wizualizacje specyficzne dla kształtu strefy */
	virtual void DrawDebugVisuals() const {}

	/** Wylicza promień kuli broadphase kolizji */
	virtual float CalculateBroadphaseRadius() const { return Radius; }

	/** Sprawdza, czy dwie strefy mogą wejść w interakcję fizyczno-chemiczną */
	virtual bool CanZonesInteract(const AStatusZoneBase* OtherZone) const;

	/** Obsługuje logikę wypierania cieczy (zwraca true, jeśli strefa ma ulec zniszczeniu) */
	virtual bool HandleLiquidDisplacement(AActor* HitInstigator);

	/** Zwraca kolor debugowy powiązany z danym statusem */
	FColor GetStatusDebugColor() const;

	/** Zwraca czytelną nazwę statusu dla napisów debugowych */
	FString GetStatusDebugName() const;

protected:
	float LastTickTime = 0.0f;
	TWeakObjectPtr<AActor> ZoneInstigator;
};

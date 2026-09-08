#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Environment/Zones/Data/ZoneTypes.h"
#include "StatusZone.generated.h"

class USphereComponent;
class UDecalComponent;
class UCharacterMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnZoneReactionSignature, EStatusEffectType, OldStatus, EStatusEffectType, NewStatus);

/**
 * Zunifikowany, stabilny aktor strefy statusu w lochu (Status Zone).
 * Dokładny, sprawdzony model działania oparty na sprawdzonej architekturze ElementalStatusZone.
 */
UCLASS()
class MYPROJECT_API AStatusZone : public AActor
{
	GENERATED_BODY()

public:
	AStatusZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * Inicjalizuje strefę na serwerze (wywoływane przez bibliotekę UStatusZoneLibrary lub spawner).
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void InitializeZone(
		const FZoneEffectConfig& InConfig,
		float InRadius,
		float InDuration,
		EZoneShapeType InShapeType = EZoneShapeType::SurfaceSplash,
		float InSurfaceHeight = 35.0f,
		const FVector& InSurfaceNormal = FVector(0.0f, 0.0f, 1.0f),
		AActor* InInstigator = nullptr);

	/** Obsługuje uderzenie żywiołem w strefę (np. ogień w plamę oleju) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage = 0.0f, AActor* HitInstigator = nullptr);

	/** Przelicza punkty obrysu strefy z uwzględnieniem kolizji ścian (Line of Sight) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Zone")
	void RebuildPerimeterPoints();

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

	// --- Zdarzenia reakcji ---
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

	/** Projektor Decal rzutujący teksturę cieczy/ognia/kwasu na ścianę lub podłogę */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDecalComponent> ZoneDecal;

	// -------------------------------------------------------------------------
	// Parametry i Debug
	// -------------------------------------------------------------------------

	/** Czy rysować czytelny obrys i timer debugowy w świecie gry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Debug")
	bool bDrawDebugZone = true;

	// -------------------------------------------------------------------------
	// Replikacja sieciowa (Zero-Bandwidth)
	// -------------------------------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_EffectConfig)
	FZoneEffectConfig EffectConfig;

	UPROPERTY(ReplicatedUsing = OnRep_Radius)
	float Radius = 300.0f;

	UPROPERTY(ReplicatedUsing = OnRep_SurfaceHeight)
	float SurfaceHeight = 35.0f;

	UPROPERTY(ReplicatedUsing = OnRep_ServerEndTime)
	float ServerEndTime = 0.0f;

	UPROPERTY(Replicated)
	float ZoneCreationTime = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_SurfaceNormal)
	FVector SurfaceNormal = FVector(0.0f, 0.0f, 1.0f);

	UPROPERTY(Replicated)
	EZoneShapeType ShapeType = EZoneShapeType::SurfaceSplash;

	UFUNCTION()
	void OnRep_EffectConfig();

	UFUNCTION()
	void OnRep_Radius();

	UFUNCTION()
	void OnRep_SurfaceHeight();

	UFUNCTION()
	void OnRep_ServerEndTime();

	UFUNCTION()
	void OnRep_SurfaceNormal();

	// -------------------------------------------------------------------------
	// Zdarzenia kolizji i logika strefy
	// -------------------------------------------------------------------------

	UFUNCTION()
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	void ProcessActiveOverlaps();
	bool IsActorEligibleForZoneEffect(AActor* TargetActor, UPrimitiveComponent* TargetComp) const;
	bool IsOverruledByNewerLiquidZone(const FVector& TargetLocation) const;
	void DrawDebugVisuals() const;

private:
	float LastTickTime = 0.0f;
	TWeakObjectPtr<AActor> ZoneInstigator;

	/** Punkty obrysu z uwzględnieniem Line-of-Sight ścian */
	mutable TArray<FVector> CachedPerimeterPoints;
};

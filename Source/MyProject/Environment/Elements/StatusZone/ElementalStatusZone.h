#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "ElementalStatusZone.generated.h"

class USphereComponent;
class UDecalComponent;

/** Kształt geometryczny strefy statusu */
UENUM(BlueprintType)
enum class EStatusZoneShapeMode : uint8
{
	/** Płaska strefa powierzchniowa dopasowana do płaszczyzny (np. kałuża na podłodze, rozbryzg na ścianie) */
	SurfaceDisk UMETA(DisplayName = "Surface Disk"),

	/** Przestrzenna strefa sferyczna (np. chmura gazu, kula wybuchu, dym) */
	Spherical   UMETA(DisplayName = "Spherical")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStatusZoneReactionSignature, EStatusEffectType, OldStatus, EStatusEffectType, NewStatus);

/**
 * Autonomiczny aktor reprezentujący strefę statusu żywiołowego w świecie gry (np. kałuża wody, plama oleju, ogień, chmura).
 * 
 * Zasady architektoniczne:
 * - Single Responsibility: Odpowiada wyłącznie za obecność żywiołu w danym wycinku przestrzeni i jego czas życia.
 * - Co-op / Networking: Zerowe obciążenie pasma (Zero-Bandwidth) – serwer replikuje ServerEndTime, klienci lokalnie liczą czas.
 * - Ochrona przed przenikaniem: Dla trybu SurfaceDisk ignoruje przestrzeń za płaszczyzną ściany (Half-Space check) + Line of Sight.
 * - Reakcje łańcuchowe: Obsługuje uderzenia innymi żywiołami (np. ogień w strefę oleju odpala pożar całej strefy).
 * - Konflikty płynów: Nowszy płyn ma pierwszeństwo w strefie nakładania się (eliminacja flickeringu).
 * - Wizualizacja LoS: Krawędź obrysu debugowego zatrzymuje się na ścianach, odzwierciedlając faktyczny zasięg płynu.
 */
UCLASS()
class MYPROJECT_API AElementalStatusZone : public AActor
{
	GENERATED_BODY()

public:
	AElementalStatusZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

	/** Inicjalizuje parametry strefy statusu na serwerze */
	UFUNCTION(BlueprintCallable, Category = "Custom|Elemental")
	void InitializeZone(
		EStatusEffectType InStatus,
		float InRadius,
		float InDuration,
		EStatusZoneShapeMode InShapeMode = EStatusZoneShapeMode::SurfaceDisk,
		const FVector& InSurfaceNormal = FVector(0.0f, 0.0f, 1.0f),
		AActor* InInstigator = nullptr);

	/** Obsługuje trafienie innym żywiołem w tę strefę (np. iskra ognia w plamę oleju) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Elemental")
	void ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage = 0.0f, AActor* HitInstigator = nullptr);

	/** Zwraca aktywny typ statusu w strefie */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	EStatusEffectType GetStatusType() const { return StatusType; }

	/** Zwraca promień strefy */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	float GetRadius() const { return Radius; }

	/** Zwraca tryb kształtu strefy */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	EStatusZoneShapeMode GetShapeMode() const { return ShapeMode; }

	/** Zwraca czas utworzenia strefy na serwerze */
	UFUNCTION(BlueprintPure, Category = "Custom|Elemental")
	float GetZoneCreationTime() const { return ZoneCreationTime; }

	/** Przelicza punkty obrysu strefy z uwzględnieniem kolizji ścian (Line of Sight) */
	UFUNCTION(BlueprintCallable, Category = "Custom|Elemental")
	void RebuildPerimeterPoints();

	// --- Zdarzenia ---
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnStatusZoneReactionSignature OnZoneReaction;

protected:
	virtual void BeginPlay() override;

	// -------------------------------------------------------------------------
	// Komponenty
	// -------------------------------------------------------------------------

	/** Strefa wyzwalająca (Trigger Sphere) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<USphereComponent> ZoneCollision;

	/** Projektor Decal rzutujący wizualną teksturę cieczy/ognia na geometrię (schody, ściany, fugi) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|Components")
	TObjectPtr<UDecalComponent> ZoneDecal;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Obrażenia na sekundę zadawane postaciom i niszczalnym drewnianym ścianom przez strefę Burning */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Damage", meta = (ClampMin = "0.0"))
	float BurnDamagePerSecond = 10.0f;

	/** Czy rysować czytelny obrys i timer debugowy w świecie gry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Debug")
	bool bDrawDebugZone = true;

	// -------------------------------------------------------------------------
	// Replikacja
	// -------------------------------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_StatusType)
	EStatusEffectType StatusType = EStatusEffectType::None;

	UPROPERTY(ReplicatedUsing = OnRep_Radius)
	float Radius = 300.0f;

	UPROPERTY(ReplicatedUsing = OnRep_ServerEndTime)
	float ServerEndTime = 0.0f;

	UPROPERTY(Replicated)
	float ZoneCreationTime = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_SurfaceNormal)
	FVector SurfaceNormal = FVector(0.0f, 0.0f, 1.0f);

	UPROPERTY(Replicated)
	EStatusZoneShapeMode ShapeMode = EStatusZoneShapeMode::SurfaceDisk;

	UFUNCTION()
	void OnRep_StatusType();

	UFUNCTION()
	void OnRep_Radius();

	UFUNCTION()
	void OnRep_ServerEndTime();

	UFUNCTION()
	void OnRep_SurfaceNormal();

	// -------------------------------------------------------------------------
	// Logika wewnętrzna
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
	mutable TArray<FVector> CachedPerimeterPoints;
};

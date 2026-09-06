#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "SurfaceStatusComponent.generated.h"

class UDamageableComponent;

/**
 * Pojedyncza plama żywiołu na powierzchni elementu architektonicznego (ściana, rampa, strop, filar).
 * Skompresowana pod kątem maksymalnej wydajności sieciowej (Zero-Bandwidth, NetQuantize).
 */
USTRUCT(BlueprintType)
struct FSurfaceStatusPatch
{
    GENERATED_BODY()

    /** Typ aktywnego statusu żywiołowego na tym fragmencie powierzchni */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceStatus")
    EStatusEffectType StatusType = EStatusEffectType::None;

    /** Punkt w przestrzeni świata, w którym uderzył żywioł */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceStatus")
    FVector_NetQuantize ImpactPoint = FVector::ZeroVector;

    /** Wektor normalny trafionej powierzchni (określa stronę ściany/filaru) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceStatus")
    FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector;

    /** Promień plamy na powierzchni w cm */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceStatus")
    float Radius = 120.0f;

    /** Czas serwera (GetTimeSeconds), w którym plama samoczynnie wygasa (Wzorzec Zero-Bandwidth) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceStatus")
    float ServerEndTime = 0.0f;

    /** Całkowity pierwotny czas trwania plamy */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceStatus")
    float TotalDuration = 5.0f;

    /** Unikalny identyfikator plamy generowany autorytatywnie na serwerze */
    UPROPERTY()
    int32 PatchID = 0;

    bool operator==(const FSurfaceStatusPatch& Other) const
    {
        return PatchID == Other.PatchID;
    }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSurfacePatchCreatedSignature, const FSurfaceStatusPatch&, Patch);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSurfacePatchRemovedSignature, const FSurfaceStatusPatch&, Patch);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnSurfaceReactionTriggeredSignature, EStatusEffectType, IncomingStatus, EStatusEffectType, ExistingStatus, FName, ReactionTag, FVector, ReactionLocation);

/**
 * Komponent zarządzający lokalnymi plamami żywiołów na powierzchniach struktur lochu (ADungeonStructureBase).
 * Zaprojektowany wg reguł kooperacji sieciowej i czystej architektury:
 * - Single Responsibility: Odpowiada wyłącznie za plamy powierzchniowe, ich geometrię i reakcje chemiczne.
 * - Zero-Bandwidth Networking: Serwer replikuje ServerEndTime, eliminując pakietowy spam w klatkach.
 * - Zero-Tick Optimization: Tick wyłączony gdy na strukturze nie ma żadnych plam, uruchamiany tylko w czasie trwania statusu.
 * - Directional Occlusion: Rozróżnia strony ściany/filaru na podstawie iloczynu skalarnego wektorów normalnych.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API USurfaceStatusComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USurfaceStatusComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // -------------------------------------------------------------------------
    // API Domenowe (Server-Authoritative)
    // -------------------------------------------------------------------------

    /** Aplikuje lokalną plamę statusu na powierzchni struktury */
    UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceStatus")
    bool ApplySurfaceStatus(
        EStatusEffectType NewStatus,
        float Duration,
        const FVector& ImpactPoint,
        const FVector& ImpactNormal,
        float Radius = 120.0f,
        AActor* InstigatorActor = nullptr);

    /** Usuwa wskazaną plamę po ID */
    UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceStatus")
    bool RemovePatch(int32 PatchID);

    /** Usuwa wszystkie plamy z powierzchni struktury */
    UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceStatus")
    void ClearAllPatches();

    /** Sprawdza, czy w danym punkcie powierzchni występuje aktywny status (z tolerancją odległości i zgodnością normalnej) */
    UFUNCTION(BlueprintPure, Category = "Custom|SurfaceStatus")
    bool HasStatusAtLocation(EStatusEffectType Status, const FVector& Location, const FVector& Normal, float Tolerance = 50.0f) const;

    /** Zwraca wszystkie aktywne plamy na strukturze */
    UFUNCTION(BlueprintPure, Category = "Custom|SurfaceStatus")
    const TArray<FSurfaceStatusPatch>& GetActivePatches() const { return ActivePatches; }

    // -------------------------------------------------------------------------
    // Delegaty Zdarzeń (Event-Driven Architecture)
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnSurfacePatchCreatedSignature OnSurfacePatchCreated;

    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnSurfacePatchRemovedSignature OnSurfacePatchRemoved;

    UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
    FOnSurfaceReactionTriggeredSignature OnSurfaceReactionTriggered;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_ActivePatches(const TArray<FSurfaceStatusPatch>& OldPatches);

    /** Aktualizuje stan uśpienia Ticku (Zero-Tick Pattern) */
    void UpdateTickState();

    /** Rysuje precyzyjne wskaźniki debugowe 3D na powierzchni (okrąg plamy + countdown timer) */
    void DrawDebugVisuals() const;

    // -------------------------------------------------------------------------
    // Konfiguracja
    // -------------------------------------------------------------------------

    /** Obrażenia na sekundę zadawane niszczalnym strukturom przez plamy Burning */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Damage", meta = (ClampMin = "0.0"))
    float BurnDamagePerSecond = 10.0f;

    /** Czy rysować debug 3D na ścianach */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Debug")
    bool bShowDebugInWorld = true;

private:
    /** Replikowana lista aktywnych plam na powierzchni tej struktury */
    UPROPERTY(ReplicatedUsing = OnRep_ActivePatches)
    TArray<FSurfaceStatusPatch> ActivePatches;

    int32 NextPatchID = 1;

    /** Czas ostatniego tiku obrażeń od ognia (DoT) */
    float LastBurnTickTime = 0.0f;

    /** Zapisany wskaźnik do komponentu wytrzymałości właściciela */
    TWeakObjectPtr<UDamageableComponent> CachedDamageableComp;

    /** Pobiera tożsamość materiałową właściciela przez IMaterialProviderInterface */
    EPhysicalMaterialType GetOwnerMaterialType() const;
};

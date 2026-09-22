#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Environment/Zones/Data/ZoneTypes.h"
#include "VolatileProp.generated.h"

/**
 * Tryb powstawania strefy po zniszczeniu/detonacji VolatileProp.
 * Jeden jednoznaczny tryb na dany Blueprint propa (czyste testowanie pojedynczych form).
 */
UENUM(BlueprintType)
enum class EVolatileZoneSpawnMode : uint8
{
    /** 1. Pełny trójwymiarowy wybuch radialny (obrażenia, odrzut, obryzganie widocznych ścian i podłogi w promieniu) */
    RadialBurst          UMETA(DisplayName = "Radial Burst (3D Explosion & Splash)"),

    /** 2. Uderzenie punktowe w pojedynczą powierzchnię (np. rzucona butelka, ampułka, koktajl, bełt) */
    PointImpact          UMETA(DisplayName = "Point Impact (Single Surface Hit)"),

    /** 3. Przestrzenna strefa 3D wisząca w powietrzu przez czas T (chmura trującego gazu, dym, mgła) */
    VolumetricZone       UMETA(DisplayName = "Volumetric Zone (3D Area)")
};

/**
 * Uniwersalny niestabilny rekwizyt lochu (beczka, mina, bomba, baniak, kryształ, butla).
 * Po zniszczeniu (spadek HP do 0 / silne zderzenie) detonuje lub uwalnia energię w wybranym trybie strefy:
 * - Radial Burst: pełny wybuch 3D z LoS, obrażenia, odrzut i powłoka na posadzce/ścianach/suficie
 * - Point Impact: uderzenie punktowe w trafioną powierzchnię (np. rzucana butelka, ampułka)
 * - Volumetric Zone: przestrzenna bryła 3D w powietrzu (gaz, dym)
 * Logika i obrażenia: Server-Authoritative.
 * Efekty wizualne i dźwiękowe: Zdarzeniowy NetMulticast.
 */
UCLASS()
class MYPROJECT_API AVolatileProp : public AInteractivePropBase
{
    GENERATED_BODY()

public:
    AVolatileProp();

    virtual void OnGrabbed(AActor* Grabber) override;
    virtual void OnDropped(AActor* Dropper, const FVector& LaunchVelocity = FVector::ZeroVector) override;

protected:
    virtual void HandleOnDestroyed(AActor* DestroyedActor) override;

    virtual void HandleImpactDamage(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, 
                                   const FHitResult& Hit) override;

    /** Lekki RPC rozsyłający do wszystkich połączonych graczy sygnał o wybuchu (FX, dźwięki, debug) */
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PlayExplosionEffects(const FVector& DetonationCenter);

    // -------------------------------------------------------------------------
    // Fizyka i Detonacja Kinetyczna
    // -------------------------------------------------------------------------

    /** Czy obiekt ma natychmiast detonować przy pierwszym zderzeniu po rzucie (klawisz R) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Kinetic")
    bool bDetonateOnThrownImpact = true;

    /** Minimalna prędkość zderzenia (cm/s) wywołująca detonację przy uderzeniu zewnętrznym (np. rzucony kamień, inna bomba, upadek z wysokości) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Kinetic", meta = (ClampMin = "50.0"))
    float MinImpactSpeedToDetonate = 300.0f;

    /** Maksymalna prędkość bezpiecznego opadania pod nogi przy upuszczeniu (klawisz E). Zapobiega wybuchowi pod nogami gracza przy lądowaniu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Kinetic", meta = (ClampMin = "300.0"))
    float MaxSafeDropSpeed = 650.0f;

    // -------------------------------------------------------------------------
    // Zasięg Efektu
    // -------------------------------------------------------------------------

    /** Promień sfery eksplozji / uwolnienia energii (w cm). Określa zasięg rażenia wybuchu, knockbacku i strefy */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile", meta = (ClampMin = "10.0"))
    float EffectRadius = 600.0f;

    // -------------------------------------------------------------------------
    // Tryb Strefy (1 jednoznaczna forma na Blueprint pod czyste testy)
    // -------------------------------------------------------------------------

    /** Wybór formy efektu: wybuch radialny 3D (Radial Burst), uderzenie punktowe (Point Impact) czy chmura 3D (Volumetric Area) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone")
    EVolatileZoneSpawnMode ZoneSpawnMode = EVolatileZoneSpawnMode::RadialBurst;

    /** Zunifikowana konfiguracja efektu (InstantDamage, KnockbackForce, AppliedStatus, DoT) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone")
    FZoneEffectConfig ZoneEffectConfig;

    /** Czas trwania trwałej strefy w sekundach (dla Surface Grid oraz Volumetric Zone) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone", meta = (ClampMin = "0.5"))
    float ZoneDuration = 8.0f;

    // -------------------------------------------------------------------------
    // Debug & Wizualizacja
    // -------------------------------------------------------------------------

    /** Czy rysować w edytorze sferę debugową ilustrującą zasięg wybuchu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Debug")
    bool bDrawDebugRadius = true;

private:
    bool bHasDetonated = false;
    bool bWasThrown = false;
    bool bDroppedSafely = false;

    /** Zapamiętany punkt uderzenia kinetycznego wywołującego detonację (dla PointImpact) */
    FHitResult LastImpactHit;
};

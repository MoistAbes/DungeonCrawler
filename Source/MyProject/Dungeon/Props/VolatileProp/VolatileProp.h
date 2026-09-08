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
    /** 1. Chwilowy wybuch / impuls LoS w klatce t0 bez tworzenia trwałego aktora strefy (granaty, bomby kinetyczne) */
    InstantBurstOnly     UMETA(DisplayName = "Instant Burst Only (One-Hit Explosion)"),

    /** 2. Płaska powłoka 10-30 cm na ścianie/podłodze podpięta pod obiekt (rozlany olej/woda, kwas) */
    SurfaceSplash        UMETA(DisplayName = "Surface Splash (10-30cm Coat)"),

    /** 3. Przestrzenna strefa 3D wisząca w powietrzu przez czas T (chmura trującego gazu, dym, mgła) */
    VolumetricZone       UMETA(DisplayName = "Volumetric Zone (3D Area)")
};

/**
 * Uniwersalny niestabilny rekwizyt lochu (beczka, mina, bomba, baniak, kryształ, butla).
 * Po zniszczeniu (spadek HP do 0 / silne zderzenie) detonuje lub uwalnia energię w wybranym trybie strefy:
 * - Instant Burst Only: jednorazowy wybuch z LoS, obrażenia i odrzut w klatce t0
 * - Surface Splash: cienka powłoka na ścianie/podłodze przyczepiona do geometrii
 * - Volumetric Zone: przestrzenna bryła 3D w powietrzu
 * Logika i obrażenia: Server-Authoritative.
 * Efekty wizualne i dźwiękowe: Zdarzeniowy NetMulticast.
 */
UCLASS()
class MYPROJECT_API AVolatileProp : public AInteractivePropBase
{
    GENERATED_BODY()

public:
    AVolatileProp();

protected:
    virtual void HandleOnDestroyed(AActor* DestroyedActor) override;

    /** Lekki RPC rozsyłający do wszystkich połączonych graczy sygnał o wybuchu (FX, dźwięki, debug) */
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PlayExplosionEffects(const FVector& DetonationCenter);

    // -------------------------------------------------------------------------
    // Zasięg Efektu
    // -------------------------------------------------------------------------

    /** Promień sfery eksplozji / uwolnienia energii (w cm). Określa zasięg rażenia wybuchu, knockbacku i strefy */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile", meta = (ClampMin = "10.0"))
    float EffectRadius = 600.0f;

    // -------------------------------------------------------------------------
    // Tryb Strefy (1 jednoznaczna forma na Blueprint pod czyste testy)
    // -------------------------------------------------------------------------

    /** Wybór formy efektu: chwilowy wybuch (Instant Burst), powłoka na ścianie/podłodze (Surface Splash) czy chmura 3D (Volumetric Area) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone")
    EVolatileZoneSpawnMode ZoneSpawnMode = EVolatileZoneSpawnMode::SurfaceSplash;

    /** Zunifikowana konfiguracja efektu (InstantDamage, KnockbackForce, AppliedStatus, DoT, MovementSpeedMultiplier) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone")
    FZoneEffectConfig ZoneEffectConfig;

    /** Czas trwania trwałej strefy w sekundach (dla Surface Splash oraz Volumetric Zone) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone", meta = (ClampMin = "0.5"))
    float ZoneDuration = 8.0f;

    /** Grubość powłoki przy ścianie/podłodze w cm (używane tylko w trybie Surface Splash, domyślnie 25 cm) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Zone", meta = (ClampMin = "10.0", ClampMax = "100.0"))
    float SurfaceSplashHeight = 25.0f;

    // -------------------------------------------------------------------------
    // Debug & Wizualizacja
    // -------------------------------------------------------------------------

    /** Czy rysować w edytorze sferę debugową ilustrującą zasięg wybuchu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Debug")
    bool bDrawDebugRadius = true;

private:
    bool bHasDetonated = false;
};

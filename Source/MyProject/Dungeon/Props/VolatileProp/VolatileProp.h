#pragma once

#include "CoreMinimal.h"
#include "MyProject/Dungeon/Props/InteractivePropBase/InteractivePropBase.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "VolatileProp.generated.h"

/**
 * Uniwersalny niestabilny rekwizyt lochu (beczka, mina, bomba, baniak, kryształ, pułapka).
 * Po zniszczeniu (spadek HP do 0 / silne zderzenie) detonuje lub uwalnia energię:
 * - Opcjonalne obrażenia fizyczne (BaseDamage)
 * - Opcjonalny radialny odrzut kinetyczny (KnockbackForce)
 * - Opcjonalny status żywiołowy (Burning, Wet, Electrified, Oiled)
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
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayExplosionEffects(const FVector& DetonationCenter);

    // -------------------------------------------------------------------------
    // Zasięg Efektu
    // -------------------------------------------------------------------------

    /** Promień sfery eksplozji / uwolnienia energii (w cm). Określa zasięg rażenia obrażeń, odrzutu i statusu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile", meta = (ClampMin = "10.0"))
    float EffectRadius = 600.0f;

    // -------------------------------------------------------------------------
    // Obrażenia i Kinetyka
    // -------------------------------------------------------------------------

    /** Bazowe obrażenia zadawane przez eksplozję (spadają liniowo wraz z odległością od centrum wybuchu) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Explosion", meta = (ClampMin = "0.0"))
    float BaseDamage = 25.0f;

    /** Siła radialnego impulsu odrzucającego obiekty fizyczne i postacie w promieniu wybuchu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Explosion", meta = (ClampMin = "0.0"))
    float KnockbackForce = 1800.0f;

    /** Czy wybuch ma aplikować radialną siłę odrzutu na postacie i inne rekwizyty fizyczne */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Explosion")
    bool bApplyKnockback = true;

    // -------------------------------------------------------------------------
    // Żywioł i Status
    // -------------------------------------------------------------------------

    /** Typ statusu żywiołowego nakładanego na cele w promieniu eksplozji (np. Burning, Wet, None) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Status")
    EStatusEffectType StatusToApply = EStatusEffectType::Burning;

    /** Czas trwania nałożonego statusu żywiołowego (w sekundach) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Status", meta = (ClampMin = "0.1"))
    float StatusDuration = 6.0f;

    // -------------------------------------------------------------------------
    // Debug & Wizualizacja
    // -------------------------------------------------------------------------

    /** Czy rysować w edytorze sferę debugową ilustrującą zasięg wybuchu */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Volatile|Debug")
    bool bDrawDebugRadius = true;

private:
    bool bHasDetonated = false;
};

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
 */
UCLASS()
class MYPROJECT_API AVolatileProp : public AInteractivePropBase
{
    GENERATED_BODY()

public:
    AVolatileProp();

protected:
    virtual void HandleOnDestroyed(AActor* DestroyedActor) override;

    // -------------------------------------------------------------------------
    // Zasięg Efektu
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Config", meta = (ClampMin = "10.0"))
    float EffectRadius = 600.0f;

    // -------------------------------------------------------------------------
    // Obrażenia i Kinetyka
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Kinetic", meta = (ClampMin = "0.0"))
    float BaseDamage = 25.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Kinetic", meta = (ClampMin = "0.0"))
    float KnockbackForce = 1800.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Kinetic")
    bool bApplyKnockback = true;

    // -------------------------------------------------------------------------
    // Żywioł i Status
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Status")
    EStatusEffectType StatusToApply = EStatusEffectType::Burning;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Status", meta = (ClampMin = "0.1"))
    float StatusDuration = 6.0f;

    // -------------------------------------------------------------------------
    // Debug & Wizualizacja
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Volatile|Debug")
    bool bDrawDebugRadius = true;

private:
    bool bHasDetonated = false;
};

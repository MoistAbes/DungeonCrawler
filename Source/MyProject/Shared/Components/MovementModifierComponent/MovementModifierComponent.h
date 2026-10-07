#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyProject/Shared/Components/MovementModifierComponent/MovementModifierTypes.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MovementModifierComponent.generated.h"

class UCharacterMovementComponent;
class UStatusEffectComponent;
class UDungeonSurfaceSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMovementModifierChanged, const FMovementModifier&, EffectiveModifier);

/**
 * Reużywalny komponent (Shared) odpowiedzialny za wyliczanie i aplikowanie modyfikatorów ruchu (prędkość, tarcie, droga hamowania).
 * Obsługuje zarówno postać gracza (PlayerCharacter), jak i przeciwników (EnemyCharacter).
 * 
 * Agreguje dwa niezależne źródła modyfikacji:
 * 1. SurfaceModifier - modyfikator komórki posadzki pod stopami (DungeonSurfaceSubsystem)
 * 2. BodyModifier - modyfikatory statusów elementarnych/wewnętrznych na ciele (StatusEffectComponent)
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYPROJECT_API UMovementModifierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMovementModifierComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Zdarzenie rozgłaszane, gdy wypadkowy modyfikator ulegnie zmianie (podstawa dla UI / Audio / VFX) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Movement")
	FOnMovementModifierChanged OnMovementModifierChanged;

	/** Zwraca aktualnie obowiązujący wypadkowy modyfikator ruchu */
	UFUNCTION(BlueprintPure, Category = "Custom|Movement")
	const FMovementModifier& GetEffectiveModifier() const { return EffectiveModifier; }

	/** Zwraca aktualny modyfikator podłoża */
	UFUNCTION(BlueprintPure, Category = "Custom|Movement")
	const FMovementModifier& GetActiveSurfaceModifier() const { return ActiveSurfaceModifier; }

	/** Zwraca aktualny modyfikator ze statusów ciała */
	UFUNCTION(BlueprintPure, Category = "Custom|Movement")
	const FMovementModifier& GetActiveBodyModifier() const { return ActiveBodyModifier; }

	/** Częstotliwość próbkowania podłoża w sekundach (domyślnie 0.05s = 20 razy na sekundę) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float SurfaceCheckInterval = 0.05f;

	/** Wymusza natychmiastowe przeliczenie wszystkich modyfikatorów i zaaplikowanie do CMC */
	UFUNCTION(BlueprintCallable, Category = "Custom|Movement")
	void RecalculateModifiers();

protected:
	/** Replikacja wypadkowego modyfikatora na klientów */
	UFUNCTION()
	void OnRep_EffectiveModifier();

	/** Nasłuchiwanie nałożenia statusu na ciele */
	UFUNCTION()
	void HandleStatusApplied(EStatusEffectType Status, float Duration);

	/** Nasłuchiwanie usunięcia statusu z ciała */
	UFUNCTION()
	void HandleStatusRemoved(EStatusEffectType Status);

	/** Natychmiastowa reakcja na uderzenie fizyczne właściciela (np. odrzut w ścianę lub sufit) */
	UFUNCTION()
	void HandleOwnerHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

	/** Próbkuje wszystkie powierzchnie (podłoga, ściany, sufit) stykające się z aktorem i odświeża ActiveSurfaceModifier */
	void UpdateSurfaceModifier();

	/** Przelicza ActiveBodyModifier na podstawie wszystkich aktywnych statusów w StatusEffectComponent */
	void UpdateBodyModifierFromStatuses();

	/** Łączy ActiveSurfaceModifier i ActiveBodyModifier w EffectiveModifier i aplikuje do CMC */
	void ApplyEffectiveModifiersToCMC();

	/** Pomocnicza metoda buforująca bazowe wartości z CMC */
	void CacheBaseValues();

private:
	/** Aktualnie obowiązujący wypadkowy modyfikator zaaplikowany do CMC */
	UPROPERTY(ReplicatedUsing = OnRep_EffectiveModifier, VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	FMovementModifier EffectiveModifier;

	/** Modyfikator pochodzący z komórki podłoża */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	FMovementModifier ActiveSurfaceModifier;

	/** Modyfikator pochodzący ze statusów na ciele */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	FMovementModifier ActiveBodyModifier;

	/** Bazowa prędkość chodu zapamiętana z CMC przed nałożeniem jakichkolwiek modyfikatorów */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	float BaseMaxWalkSpeed = 600.0f;

	/** Bazowe tarcie podłoża zapamiętane z CMC */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	float BaseGroundFriction = 8.0f;

	/** Bazowa siła hamowania chodu zapamiętana z CMC */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	float BaseBrakingDecelerationWalking = 2048.0f;

	/** Bazowe hamowanie w powietrzu zapamiętane z CMC */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	float BaseBrakingDecelerationFalling = 0.0f;

	/** Bazowe tarcie boczne w powietrzu zapamiętane z CMC */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Custom|Movement", meta = (AllowPrivateAccess = "true"))
	float BaseFallingLateralFriction = 0.0f;

	/** Czy wartości bazowe zostały pomyślnie zbuforowane z CMC */
	bool bBaseValuesCached = false;

	/** Wskaźnik do komponentu ruchu właściciela */
	TWeakObjectPtr<UCharacterMovementComponent> CachedMovementComponent;

	/** Wskaźnik do komponentu statusów właściciela */
	TWeakObjectPtr<UStatusEffectComponent> CachedStatusComponent;

	/** Wskaźnik do podsystemu siatek powierzchni lochu */
	TWeakObjectPtr<UDungeonSurfaceSubsystem> CachedSurfaceSubsystem;

	/** Ostatnia sprawdzona pozycja stóp - optymalizacja pod pomijanie zbędnych zapytań o siatkę w spoczynku */
	FVector LastFloorCheckLocation = FVector(TNumericLimits<float>::Max());
};

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MyProject/Environment/Zones/Data/SurfaceGridTypes.h"
#include "DungeonSurfaceSubsystem.generated.h"

class ACharacter;
class UStatusEffectComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSurfaceCellChanged, const FSurfaceCellCoord&, Coord, EStatusEffectType, NewStatus, AActor*, Instigator);

/**
 * Podsystem świata zarządzający rzadką siatką komórek powierzchniowych (Sparse World Surface Grid).
 * 
 * Zastępuje analityczne obrysy radialne stref powierzchniowych.
 * Odpowiada za:
 * - Przechowywanie aktywnych komórek w pamięci podręcznej (TMap) bez alokacji osobnych aktorów.
 * - Mapowanie uderzeń cieczy/ognia na dyskretne komórki z uwzględnieniem strony fundamentu (Face Direction).
 * - Ewaluację reakcji chemicznych między żywiołami (np. Ogień + Olej = Płomień).
 * - Błyskawiczne usuwanie komórek z obszaru zniszczonych fundamentów (ClearCellsInBounds).
 * - Cykliczną aplikację statusów na wchodzące postacie oraz wygaszanie przeterminowanych komórek.
 */
UCLASS()
class MYPROJECT_API UDungeonSurfaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDungeonSurfaceSubsystem();

	// --- Cykl Życia Subsystemu ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// -------------------------------------------------------------------------
	// Konfiguracja
	// -------------------------------------------------------------------------

	/** Fizyczny rozmiar pojedynczej komórki w centymetrach (domyślnie 50 cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float CellSize = 50.0f;

	/** Interwał serwera sprawdzający obecność postaci na aktywnych komórkach i wygaszanie (w sekundach) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Custom|SurfaceGrid", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SubsystemTickInterval = 0.25f;

	/** Flaga włączająca debugowe rysowanie aktywnych komórek w edytorze i trybach deweloperskich */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|Debug")
	bool bDrawDebugGrid = true;

	// -------------------------------------------------------------------------
	// Główne API Domenowe
	// -------------------------------------------------------------------------

	/**
	 * Kwalifikuje, czy dany aktor może być podłożem pod komórki powierzchniowe.
	 * Akceptuje fundamenty lochu (ADungeonStructureBase) oraz geometrię poziomu (ABrush).
	 * Odrzuca dynamiczne rekwizyty (AInteractivePropBase) oraz postacie (APawn).
	 */
	UFUNCTION(BlueprintPure, Category = "Custom|SurfaceGrid")
	static bool IsValidSurfaceTarget(const AActor* Actor);

	/**
	 * Rozwiązuje uderzenie żywiołem w świecie (Hit Resolver):
	 * - Trafienie w postać/rekwizyt: nakłada status na cel i szuka podłogi pod jego stopami (FloorTrace), nakładając status na podłoże.
	 * - Trafienie w strefę przestrzenną (np. chmurę): przekazuje trafienie żywiołowe strefie.
	 * - Trafienie w ścianę/podłogę: weryfikuje podłoże i wywołuje ApplyStatusToSurface.
	 * 
	 * @return Liczba zmodyfikowanych komórek powierzchniowych.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyStatusFromHit(
		const FHitResult& HitResult,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * Nakłada status na komórki pojedynczej powierzchni wokół punktu uderzenia.
	 * Wyznacza komórki w promieniu Radius, uwzględnia orientację ściany/podłogi,
	 * sprawdza Line of Sight po powierzchni i przeprowadza ewaluację reakcji chemicznych.
	 * Używane przez PointImpact (np. rozbicie flakonu na ścianie).
	 * 
	 * @return Liczba zmodyfikowanych lub zaktualizowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyStatusToSurface(
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * JEDYNY ATOMOWY PUNKT STYKU (Single Point of Truth) dla stanu komórki w siatce.
	 * Przyjmuje status z dowolnego źródła (wybuch, plama, pocisk, postać, propagacja),
	 * odpytuje reguły UElementalReactionRules, aplikuje wynik reakcji i zarządza ActiveCells.
	 *
	 * @return true jeśli komórka została zmodyfikowana (dodana, odświeżona, przereagowana lub usunięta).
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	bool ApplyStatusToCell(
		const FSurfaceCellCoord& Coord,
		EStatusEffectType IncomingStatus,
		float Duration,
		AActor* Instigator = nullptr,
		EPhysicalMaterialType ExplicitMaterial = EPhysicalMaterialType::Stone,
		AActor* SurfaceActor = nullptr,
		uint8 Tier = 0);

	/**
	 * Nakłada status na komórki w obszarze 3D wokół źródła wybuchu (RadialBurst).
	 * Sprawdza Line of Sight z BurstOrigin do każdej komórki, obsługuje wiele powierzchni jednocześnie.
	 * Używa ProcessedCoords do uniknięcia wielokrotnego przetwarzania tej samej komórki
	 * w ramach jednego złożonego zdarzenia (np. wybuchu wielopromieniowego).
	 */
	int32 ApplyStatusInArea(
		const FVector& HitLocation,
		const FVector& HitNormal,
		float Radius,
		EStatusEffectType Status,
		float Duration,
		const FVector& BurstOrigin,
		TSet<FSurfaceCellCoord>& ProcessedCoords,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/**
	 * Usuwa wszystkie aktywne komórki znajdujące się wewnątrz zadanego prostopadłościanu AABB.
	 * Wywoływane automatycznie przez ADungeonStructureBase w momencie zniszczenia ściany lub podłogi.
	 * 
	 * @return Liczba usuniętych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ClearCellsInBounds(const FBox& BoundingBox);

	/**
	 * Aplikuje impuls żywiołowy w sferze o zadanym promieniu (wybuch beczki, czar obszarowy).
	 * Wywołuje reakcje chemiczne ze wszystkimi komórkami w zasięgu oraz opcjonalnie maluje strefę na trafionej posadzce.
	 * 
	 * @return Liczba zaktualizowanych lub pomalowanych komórek.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom|SurfaceGrid")
	int32 ApplyElementalBurst(
		const FVector& Origin,
		float Radius,
		EStatusEffectType Status,
		float Duration = 5.0f,
		AActor* Instigator = nullptr,
		uint8 Tier = 0);

	/** Zdarzenie wywoływane przy zmianie stanu komórki (podstawa dla systemów VFX/SFX) */
	UPROPERTY(BlueprintAssignable, Category = "Custom|Events")
	FOnSurfaceCellChanged OnSurfaceCellChanged;

	/** Rejestruje komponent statusów do cyklicznej ewaluacji z siatką powierzchni */
	void RegisterStatusComponent(UStatusEffectComponent* Comp);

	/** Wyrejestrowuje komponent statusów z podsystemu */
	void UnregisterStatusComponent(UStatusEffectComponent* Comp);

protected:
	/** Okresowa pętla serwera: wygaszanie starych komórek i aplikacja statusów na postacie */
	UFUNCTION()
	void ProcessGridTick();

	/** Rysuje debugowe wizualizacje aktywnych komórek w świecie */
	void DrawDebugVisuals() const;

private:
	/** Wygasza przeterminowane statusy w aktywnych komórkach siatki */
	void ExpireCellStatuses(float CurrentTime);

	/** Propaguje żywioły na sąsiednie komórki (Cellular Automata) */
	void PropagateElementalSpreads(float CurrentTime, float SafeCellSize);

	/** Rozprzestrzenia stały ogień po materiale stanowiącym paliwo (bSelfSustainingFuel, np. drewno) */
	void ProcessSolidFuelCombustion(float CurrentTime, float SafeCellSize);

	/** Aplikuje zagregowane obrażenia od aktywnych komórek żywiołów do fundamentów architektury lochu */
	void ProcessSurfaceStructuralDamage(float CurrentTime, float DeltaTime);

	/** Ewaluuje dwukierunkową interakcję z zarejestrowanymi komponentami statusów */
	void ProcessActorInteractions(float CurrentTime);

	/** Przetwarza interakcję pojedynczego aktora ze stykającymi się komórkami */
	void ProcessActorInteraction(AActor* Actor, UStatusEffectComponent* StatusComp, float CurrentTime);

	/** Faza A: Aktor wpływa na stykające się komórki (np. podpalenie plamy oleju, gaszenie wodą) */
	void ApplyActorEffectsToFloor(
		AActor* Actor,
		UStatusEffectComponent* StatusComp,
		const TArray<FSurfaceCellCoord>& TouchedCells,
		TArray<EStatusEffectType>& InOutActorStatuses);

	/** Faza B: Podłoże wpływa na aktora (aplikacja dominującej cieczy i wtórnych statusów podłogi) */
	void ApplyFloorEffectsToActor(
		AActor* Actor,
		UStatusEffectComponent* StatusComp,
		const TArray<FSurfaceCellCoord>& TouchedCells);

	/** Zwraca współrzędne wszystkich komórek, z którymi w danej chwili styka się bryła kolizyjna lub stopy aktora */
	void GetCellsTouchingActor(const AActor* Actor, TArray<FSurfaceCellCoord>& OutCoords) const;

	/** Pobiera materiał fizyczny architektury lochu pod daną komórką powierzchniową. Zwraca false jeśli brak fizycznej geometrii. */
	bool GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial) const;

	/** Pobiera materiał fizyczny oraz wskaźnik do aktora architektury lochu pod daną komórką powierzchniową. */
	bool GetSurfaceMaterialAtCoord(const FSurfaceCellCoord& Coord, EPhysicalMaterialType& OutMaterial, AActor*& OutSurfaceActor) const;

	/** Rejestr aktywnych komponentów statusów w świecie podlegających interakcji z podłożem */
	UPROPERTY()
	TArray<TWeakObjectPtr<UStatusEffectComponent>> RegisteredStatusComponents;

	/** Rzadka mapa aktywnych komórek powierzchniowych */
	TMap<FSurfaceCellCoord, FSurfaceCellData> ActiveCells;

	/** Uchwyt timera serwerowego */
	FTimerHandle GridTickTimerHandle;
};

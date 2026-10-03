#include "VolumetricStatusZone.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MyProject/Environment/Zones/Subsystems/DungeonSurfaceSubsystem.h"

AVolumetricStatusZone::AVolumetricStatusZone()
{
}

void AVolumetricStatusZone::InitializeVolumetricZone(
	const FZoneEffectConfig& InConfig,
	float InRadius,
	float InDuration,
	AActor* InInstigator)
{
	// Czysta inicjalizacja strefy bazowej bez jednorazowego efektu wybuchu radialnego.
	// Emisja statusu na obiekty i komórki odbywa się naturalnie w ProcessActiveOverlaps.
	InitializeZoneBase(InConfig, InRadius, InDuration, InInstigator);
}

void AVolumetricStatusZone::ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage, AActor* HitInstigator)
{
	// Strefa wolumetryczna jest trwałym, niezmiennym emiterem pola żywiołowego.
	// Nie przyjmuje obcych statusów, nie ulega mutacji ani nie dokłada statusów do swojej definicji.
}

void AVolumetricStatusZone::ProcessActiveOverlaps()
{
	Super::ProcessActiveOverlaps();

	// Ciągła emisja statusu strefy na komórki powierzchniowe lochu w jej geometrycznym zasięgu 3D i Line of Sight
	if (EffectConfig.AppliedStatus != EStatusEffectType::None)
	{
		if (UWorld* World = GetWorld())
		{
			if (UDungeonSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UDungeonSurfaceSubsystem>())
			{
				const float CurrentTime = World->GetTimeSeconds();
				const float RemainingTime = (ServerEndTime > 0.0f) ? FMath::Max(ZoneTickInterval * 2.0f, ServerEndTime - CurrentTime) : 5.0f;
				SurfaceSubsystem->ApplyElementalBurst(GetActorLocation(), Radius, EffectConfig.AppliedStatus, RemainingTime, ZoneInstigator.Get(), EffectConfig.StatusTier);
			}
		}
	}
}

bool AVolumetricStatusZone::IsActorWithinZoneGeometry(const FBoxSphereBounds& Bounds) const
{
	// Wyliczamy rzeczywistą minimalną odległość od środka sfery do bryły kolizyjnej celu (AABB)
	const float DistSq = Bounds.GetBox().ComputeSquaredDistanceToPoint(GetActorLocation());
	return DistSq <= FMath::Square(Radius);
}

void AVolumetricStatusZone::DrawDebugVisuals() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDrawDebugZone || !GetWorld())
	{
		return;
	}

	const float Remaining = FMath::Max(0.0f, ServerEndTime - GetWorld()->GetTimeSeconds());
	const FColor ZoneColor = GetStatusDebugColor();
	const FString StatusName = GetStatusDebugName();
	const FVector Center = GetActorLocation();
	const float DebugLifeTime = ZoneTickInterval + 0.05f;
	DrawDebugSphere(GetWorld(), Center, Radius, 16, ZoneColor, false, DebugLifeTime, 0, 2.0f);
	const FVector TextPos = Center + FVector(0.0f, 0.0f, 30.0f);
	DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, DebugLifeTime, true, 1.2f);
#endif
}

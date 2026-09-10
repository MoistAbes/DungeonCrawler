#include "VolumetricStatusZone.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AVolumetricStatusZone::AVolumetricStatusZone()
{
	ShapeType = EZoneShapeType::VolumetricSphere;
}

void AVolumetricStatusZone::InitializeVolumetricZone(
	const FZoneEffectConfig& InConfig,
	float InRadius,
	float InDuration,
	AActor* InInstigator)
{
	InitializeZoneBase(InConfig, InRadius, InDuration, EZoneShapeType::VolumetricSphere, InInstigator);
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

	DrawDebugSphere(GetWorld(), Center, Radius, 16, ZoneColor, false, 0.0f, 0, 2.0f);
	const FVector TextPos = Center + FVector(0.0f, 0.0f, 30.0f);
	DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, 0.0f, true, 1.2f);
#endif
}

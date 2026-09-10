#include "SurfaceSplashZone.h"

#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Environment/Zones/Utilities/StatusZoneLibrary.h"

ASurfaceSplashZone::ASurfaceSplashZone()
{
	SurfaceHeight = 35.0f;
	SurfaceNormal = FVector::UpVector;
	ShapeType = EZoneShapeType::SurfaceSplash;

	ZoneDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("ZoneDecal"));
	ZoneDecal->SetupAttachment(RootComponent);
	ZoneDecal->DecalSize = FVector(50.0f, Radius, Radius);
	ZoneDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
}

void ASurfaceSplashZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASurfaceSplashZone, SurfaceNormal);
	DOREPLIFETIME(ASurfaceSplashZone, SurfaceHeight);
}

void ASurfaceSplashZone::BeginPlay()
{
	Super::BeginPlay();

	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::InitializeSurfaceSplash(
	const FZoneEffectConfig& InConfig,
	float InRadius,
	float InDuration,
	float InSurfaceHeight,
	const FVector& InSurfaceNormal,
	AActor* InInstigator)
{
	SurfaceHeight = FMath::Clamp(InSurfaceHeight, 10.0f, 120.0f);
	SurfaceNormal = InSurfaceNormal.IsNearlyZero() ? FVector::UpVector : InSurfaceNormal.GetSafeNormal();

	InitializeZoneBase(InConfig, InRadius, InDuration, EZoneShapeType::SurfaceSplash, InInstigator);

	if (ZoneDecal)
	{
		ZoneDecal->SetVisibility(true);
		const float ProjectionDepth = FMath::Max(SurfaceHeight * 2.0f, 60.0f);
		ZoneDecal->DecalSize = FVector(ProjectionDepth, Radius, Radius);
		ZoneDecal->SetWorldRotation(FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator());
	}

	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::MergeWithZone(float InDuration, float RadiusGrowthMultiplier, float MaxRadiusCap)
{
	Super::MergeWithZone(InDuration, RadiusGrowthMultiplier, MaxRadiusCap);

	if (ZoneDecal)
	{
		const float ProjectionDepth = FMath::Max(SurfaceHeight * 2.0f, 60.0f);
		ZoneDecal->DecalSize = FVector(ProjectionDepth, Radius, Radius);
	}

	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::OnRep_Radius()
{
	Super::OnRep_Radius();

	if (ZoneDecal)
	{
		ZoneDecal->DecalSize = FVector(SurfaceHeight * 2.0f, Radius, Radius);
	}
	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::OnRep_SurfaceNormal()
{
	if (ZoneDecal)
	{
		ZoneDecal->SetWorldRotation(FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator());
	}
	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::OnRep_SurfaceHeight()
{
	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(CalculateBroadphaseRadius());
	}
	if (ZoneDecal)
	{
		ZoneDecal->DecalSize = FVector(SurfaceHeight * 2.0f, Radius, Radius);
	}
}

float ASurfaceSplashZone::GetMaxAllowedHeight() const
{
	return (EffectConfig.AppliedStatus == EStatusEffectType::Burning) ? 85.0f : SurfaceHeight;
}

float ASurfaceSplashZone::CalculateBroadphaseRadius() const
{
	const float MaxHeight = GetMaxAllowedHeight();
	// Promień kuli opisanej na cylindrze: sqrt(R^2 + H^2) + margines bezpieczeństwa 15 cm
	return FMath::Sqrt(FMath::Square(Radius) + FMath::Square(MaxHeight)) + 15.0f;
}

bool ASurfaceSplashZone::IsPerimeterCacheValid() const
{
	if (CachedPerimeterPoints.Num() < 3 || CachedPerimeterDistances.Num() < 3)
	{
		return false;
	}

	if (FVector::DistSquared(GetActorLocation(), CachedCenter) > 4.0f) // Tolerancja 2 cm na przesunięcie rodzica
	{
		return false;
	}

	if (FVector::DistSquared(SurfaceNormal, CachedNormal) > 0.001f)
	{
		return false;
	}

	if (!FMath::IsNearlyEqual(Radius, CachedRadius, 0.5f))
	{
		return false;
	}

	return true;
}

void ASurfaceSplashZone::RebuildPerimeterPoints()
{
	CachedPerimeterPoints.Reset();
	CachedPerimeterDistances.Reset();

	if (!GetWorld() || Radius <= 0.0f)
	{
		CachedCenter = FVector(NAN);
		CachedNormal = FVector(NAN);
		CachedRadius = -1.0f;
		return;
	}

	const FVector Center = GetActorLocation();
	CachedCenter = Center;
	CachedNormal = SurfaceNormal;
	CachedRadius = Radius;

	const FMatrix SurfaceMatrix = FRotationMatrix::MakeFromX(SurfaceNormal);
	const FVector AxisY = SurfaceMatrix.GetScaledAxis(EAxis::Y);
	const FVector AxisZ = SurfaceMatrix.GetScaledAxis(EAxis::Z);

	// Uniesienie 25 cm nad posadzkę do trace'owania
	const float LiftOffset = 25.0f;
	const FVector TraceStart = Center + SurfaceNormal * LiftOffset;

	constexpr int32 NumSegments = 48;
	CachedPerimeterPoints.Reserve(NumSegments);
	CachedPerimeterDistances.Reserve(NumSegments);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(StatusZonePerimeterTrace), false, this);
	TraceParams.AddIgnoredActor(this);
	if (ZoneInstigator.IsValid())
	{
		TraceParams.AddIgnoredActor(ZoneInstigator.Get());
	}

	// Pomocnik weryfikujący obecność podłoża (Edge & Drop-Off Detection)
	auto IsSurfacePresentAt = [&](const FVector& RayDir, float Dist) -> bool
	{
		const FVector ProbeCenter = Center + RayDir * Dist;
		const FVector ProbeStart = ProbeCenter + SurfaceNormal * LiftOffset;
		const FVector ProbeEnd = ProbeCenter - SurfaceNormal * (LiftOffset + 15.0f);

		FHitResult ProbeHit;
		if (GetWorld()->LineTraceSingleByChannel(ProbeHit, ProbeStart, ProbeEnd, ECC_WorldStatic, TraceParams))
		{
			if (ProbeHit.GetActor() && UStatusZoneLibrary::IsValidSurfaceTarget(ProbeHit.GetActor()))
			{
				const float NormalDot = FVector::DotProduct(ProbeHit.ImpactNormal, SurfaceNormal);
				if (NormalDot > 0.65f)
				{
					const float DistFromPlane = FMath::Abs(FVector::DotProduct(ProbeHit.ImpactPoint - ProbeCenter, SurfaceNormal));
					if (DistFromPlane < 25.0f)
					{
						return true;
					}
				}
			}
		}
		return false;
	};

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * (360.0f / static_cast<float>(NumSegments)));
		const FVector RayDir = (FMath::Cos(AngleRad) * AxisY + FMath::Sin(AngleRad) * AxisZ).GetSafeNormal();
		const FVector TraceEnd = TraceStart + RayDir * Radius;

		float MaxAllowedDist = Radius;

		// 1. Weryfikacja przeszkód wyrastających z powierzchni (ściany, filary, progi)
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
		{
			if (Hit.bStartPenetrating || Hit.Distance < 20.0f)
			{
				FHitResult InwardHit;
				if (GetWorld()->LineTraceSingleByChannel(InwardHit, TraceEnd, TraceStart, ECC_WorldStatic, TraceParams))
				{
					const bool bInwardIsObstacle = FMath::Abs(FVector::DotProduct(InwardHit.ImpactNormal, SurfaceNormal)) < 0.6f;
					if (bInwardIsObstacle)
					{
						const float InwardDist = FVector::DotProduct(InwardHit.ImpactPoint - Center, RayDir);
						MaxAllowedDist = FMath::Clamp(InwardDist, 0.0f, Radius);
					}
				}
			}
			else
			{
				const bool bIsObstacle = FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) < 0.6f;
				if (bIsObstacle)
				{
					const float HitDist = FVector::DotProduct(Hit.ImpactPoint - Center, RayDir);
					MaxAllowedDist = FMath::Clamp(HitDist, 0.0f, Radius);
				}
			}
		}

		// 2. Weryfikacja obecności podłoża (Edge & Drop-Off Detection)
		if (MaxAllowedDist > 15.0f && !IsSurfacePresentAt(RayDir, MaxAllowedDist))
		{
			float Low = 0.0f;
			float High = MaxAllowedDist;
			for (int32 Step = 0; Step < 4; ++Step)
			{
				const float Mid = (Low + High) * 0.5f;
				if (IsSurfacePresentAt(RayDir, Mid))
				{
					Low = Mid;
				}
				else
				{
					High = Mid;
				}
			}
			MaxAllowedDist = Low;
		}

		CachedPerimeterPoints.Add(Center + RayDir * MaxAllowedDist);
		CachedPerimeterDistances.Add(MaxAllowedDist);
	}
}

float ASurfaceSplashZone::GetPerimeterRadiusAtAngle(float AngleRad) const
{
	if (!IsPerimeterCacheValid())
	{
		const_cast<ASurfaceSplashZone*>(this)->RebuildPerimeterPoints();
	}

	const int32 NumPoints = CachedPerimeterDistances.Num();
	if (NumPoints < 3)
	{
		return Radius;
	}

	float NormalizedAngle = FMath::Fmod(AngleRad, 2.0f * PI);
	if (NormalizedAngle < 0.0f)
	{
		NormalizedAngle += 2.0f * PI;
	}

	const float StepRad = (2.0f * PI) / static_cast<float>(NumPoints);
	const float SegmentFloat = NormalizedAngle / StepRad;
	const int32 i0 = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, NumPoints - 1);
	const int32 i1 = (i0 + 1) % NumPoints;
	const float Fraction = SegmentFloat - static_cast<float>(i0);

	const float Dist0 = CachedPerimeterDistances[i0];
	const float Dist1 = CachedPerimeterDistances[i1];

	// Interpolacja kątowa z ograniczeniem [0, Radius]. 0.0f oznacza brak powierzchni (krawędź przepaści/dziura)
	return FMath::Clamp(FMath::Lerp(Dist0, Dist1, Fraction), 0.0f, Radius);
}

bool ASurfaceSplashZone::IsActorWithinZoneGeometry(const FBoxSphereBounds& Bounds) const
{
	if (!IsPerimeterCacheValid())
	{
		const_cast<ASurfaceSplashZone*>(this)->RebuildPerimeterPoints();
	}

	const FVector ZoneCenter = GetActorLocation();
	const FVector BoundsOrigin = Bounds.Origin;
	const FVector Extent = Bounds.BoxExtent;

	// 1. Weryfikacja wzdłuż wektora normalnego powierzchni (grubość powłoki / wysokość)
	const float ProjectedHalfExtent = FMath::Abs(SurfaceNormal.X) * Extent.X +
	                                  FMath::Abs(SurfaceNormal.Y) * Extent.Y +
	                                  FMath::Abs(SurfaceNormal.Z) * Extent.Z;

	const float DistNormal = FVector::DotProduct(BoundsOrigin - ZoneCenter, SurfaceNormal);
	const float MinDist = DistNormal - ProjectedHalfExtent;
	const float MaxDist = DistNormal + ProjectedHalfExtent;

	if (MaxDist < -15.0f)
	{
		return false;
	}

	const float MaxAllowedHeight = GetMaxAllowedHeight();
	if (MinDist > MaxAllowedHeight)
	{
		return false;
	}

	// 2. Weryfikacja w płaszczyźnie stycznej powierzchni (promień powłoki i obrys z uwzględnieniem wycięć)
	const FVector ToBounds = BoundsOrigin - ZoneCenter;
	const FVector TangentialVec = ToBounds - DistNormal * SurfaceNormal;
	const float TangentialDist = TangentialVec.Size();

	if (TangentialDist > 1.0f)
	{
		const FVector TangentialDir = TangentialVec / TangentialDist;
		const float TangentialExtent = FMath::Abs(TangentialDir.X) * Extent.X +
		                               FMath::Abs(TangentialDir.Y) * Extent.Y +
		                               FMath::Abs(TangentialDir.Z) * Extent.Z;

		if (CachedPerimeterPoints.Num() < 3)
		{
			const_cast<ASurfaceSplashZone*>(this)->RebuildPerimeterPoints();
		}

		const FMatrix SurfaceMatrix = FRotationMatrix::MakeFromX(SurfaceNormal);
		const FVector AxisY = SurfaceMatrix.GetScaledAxis(EAxis::Y);
		const FVector AxisZ = SurfaceMatrix.GetScaledAxis(EAxis::Z);

		const float AngleY = FVector::DotProduct(TangentialDir, AxisY);
		const float AngleZ = FVector::DotProduct(TangentialDir, AxisZ);
		const float AngleRad = FMath::Atan2(AngleZ, AngleY);

		const float AllowedRadius = GetPerimeterRadiusAtAngle(AngleRad);

		if (TangentialDist - TangentialExtent > AllowedRadius)
		{
			return false;
		}
	}

	return true;
}

bool ASurfaceSplashZone::CanZonesInteract(const AStatusZoneBase* OtherZone) const
{
	if (!Super::CanZonesInteract(OtherZone))
	{
		return false;
	}

	// Jeśli druga strefa też jest plamą powierzchniową, weryfikujemy rzeczywisty styk fizyczny
	if (const ASurfaceSplashZone* OtherSplash = Cast<ASurfaceSplashZone>(const_cast<AStatusZoneBase*>(OtherZone)))
	{
		const float NormalDot = FVector::DotProduct(SurfaceNormal, OtherSplash->GetSurfaceNormal());
		const float DistToOtherPlane = FMath::Abs(FVector::DotProduct(GetActorLocation() - OtherSplash->GetActorLocation(), OtherSplash->GetSurfaceNormal()));
		const float DistToThisPlane = FMath::Abs(FVector::DotProduct(OtherSplash->GetActorLocation() - GetActorLocation(), SurfaceNormal));

		if (NormalDot > 0.85f && DistToThisPlane < 30.0f)
		{
			const float DistSq = FVector::DistSquared(GetActorLocation(), OtherSplash->GetActorLocation());
			return DistSq <= FMath::Square(Radius + OtherSplash->GetRadius());
		}

		// Różne/prostopadłe płaszczyzny (np. podłoga i ściana)
		const bool bThisReachesOther = DistToOtherPlane <= (Radius + OtherSplash->GetMaxAllowedHeight());
		const bool bOtherReachesThis = DistToThisPlane <= (OtherSplash->GetRadius() + GetMaxAllowedHeight());
		return bThisReachesOther && bOtherReachesThis;
	}

	return true;
}

bool ASurfaceSplashZone::HandleLiquidDisplacement(AActor* HitInstigator)
{
	if (const ASurfaceSplashZone* OtherSplash = Cast<ASurfaceSplashZone>(HitInstigator))
	{
		const float NormalDot = FVector::DotProduct(SurfaceNormal, OtherSplash->GetSurfaceNormal());
		const float PlaneDist = FMath::Abs(FVector::DotProduct(GetActorLocation() - OtherSplash->GetActorLocation(), SurfaceNormal));
		return (NormalDot > 0.85f && PlaneDist < 30.0f);
	}
	return true;
}

void ASurfaceSplashZone::DrawDebugVisuals() const
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

	if (!IsPerimeterCacheValid())
	{
		const_cast<ASurfaceSplashZone*>(this)->RebuildPerimeterPoints();
	}

	float MaxDistSq = 0.0f;
	for (const FVector& Pt : CachedPerimeterPoints)
	{
		MaxDistSq = FMath::Max(MaxDistSq, FVector::DistSquared(Pt, Center));
	}

	const int32 NumPoints = CachedPerimeterPoints.Num();
	constexpr float DebugVisualOffset = 4.0f;
	const FVector VisualOffset = SurfaceNormal * DebugVisualOffset;

	if (NumPoints >= 3 && MaxDistSq >= 2500.0f)
	{
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const FVector Pt1 = CachedPerimeterPoints[i] + VisualOffset;
			const FVector Pt2 = CachedPerimeterPoints[(i + 1) % NumPoints] + VisualOffset;
			DrawDebugLine(GetWorld(), Pt1, Pt2, ZoneColor, false, 0.0f, 0, 4.0f);
		}
	}
	else
	{
		FMatrix Matrix = FRotationMatrix::MakeFromX(SurfaceNormal);
		Matrix.SetOrigin(Center + VisualOffset);
		DrawDebugCircle(GetWorld(), Matrix, Radius, 48, ZoneColor, false, 0.0f, 0, 4.0f, false);
	}

	const FVector TextPos = Center + SurfaceNormal * 30.0f;
	DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, 0.0f, true, 1.2f);
#endif
}

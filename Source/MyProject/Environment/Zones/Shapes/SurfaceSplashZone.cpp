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

	// Projektor Decal jest wyłączony, aby nie rzutować pustego białego materiału na fundamenty
	ZoneDecal = nullptr;
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
		ZoneDecal->SetVisibility(false);
	}
	UpdateDecalTransform();

	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::MergeWithZone(float InDuration, float RadiusGrowthMultiplier, float MaxRadiusCap)
{
	Super::MergeWithZone(InDuration, RadiusGrowthMultiplier, MaxRadiusCap);

	UpdateDecalTransform();
	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::OnRep_Radius()
{
	Super::OnRep_Radius();

	UpdateDecalTransform();
	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::OnRep_SurfaceNormal()
{
	UpdateDecalTransform();
	RebuildPerimeterPoints();
}

void ASurfaceSplashZone::OnRep_SurfaceHeight()
{
	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(CalculateBroadphaseRadius());
	}
	UpdateDecalTransform();
}

void ASurfaceSplashZone::UpdateDecalTransform()
{
	if (ZoneDecal)
	{
		ZoneDecal->SetVisibility(false);
	}
}

bool ASurfaceSplashZone::IsCoplanarWithPoint(const FVector& OtherLocation, const FVector& OtherNormal, float ToleranceDist, float MinDot) const
{
	const float NormalDot = FVector::DotProduct(SurfaceNormal, OtherNormal);
	if (NormalDot < MinDot)
	{
		return false;
	}

	const float PlaneDist = FMath::Abs(FVector::DotProduct(OtherLocation - GetActorLocation(), SurfaceNormal));
	return PlaneDist <= ToleranceDist;
}

bool ASurfaceSplashZone::IsCoplanarWithZone(const ASurfaceSplashZone* OtherSplash, float ToleranceDist, float MinDot) const
{
	if (!OtherSplash)
	{
		return false;
	}
	return IsCoplanarWithPoint(OtherSplash->GetActorLocation(), OtherSplash->GetSurfaceNormal(), ToleranceDist, MinDot);
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

bool ASurfaceSplashZone::CheckSurfacePresentAt(
	const FVector& ProbeCenter,
	float LiftOffset,
	const FCollisionQueryParams& TraceParams) const
{
	if (!GetWorld())
	{
		return false;
	}

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
				return (DistFromPlane < 25.0f);
			}
		}
	}
	return false;
}

float ASurfaceSplashZone::TraceObstacleDistance(
	const FVector& Center,
	const FVector& TraceStart,
	const FVector& RayDir,
	float MaxDist,
	const FCollisionQueryParams& TraceParams) const
{
	if (!GetWorld())
	{
		return MaxDist;
	}

	const FVector TraceEnd = TraceStart + RayDir * MaxDist;

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
	{
		return MaxDist;
	}

	// Przypadek A: Promień zaczyna się wewnątrz geometrii (np. kolumna lub niski próg)
	if (Hit.bStartPenetrating || Hit.Distance < 20.0f)
	{
		FHitResult InwardHit;
		if (GetWorld()->LineTraceSingleByChannel(InwardHit, TraceEnd, TraceStart, ECC_WorldStatic, TraceParams))
		{
			const bool bInwardIsObstacle = FMath::Abs(FVector::DotProduct(InwardHit.ImpactNormal, SurfaceNormal)) < 0.6f;
			if (bInwardIsObstacle)
			{
				const float InwardDist = FVector::DotProduct(InwardHit.ImpactPoint - Center, RayDir);
				return FMath::Clamp(InwardDist, 0.0f, MaxDist);
			}
		}
		return MaxDist;
	}

	// Przypadek B: Standardowe uderzenie w przeszkodę (ściana, filar)
	const bool bIsObstacle = FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) < 0.6f;
	if (bIsObstacle)
	{
		const float HitDist = FVector::DotProduct(Hit.ImpactPoint - Center, RayDir);
		return FMath::Clamp(HitDist, 0.0f, MaxDist);
	}

	return MaxDist;
}

float ASurfaceSplashZone::FindDropOffEdgeDistance(
	const FVector& Center,
	const FVector& RayDir,
	float InitialMaxDist,
	float LiftOffset,
	const FCollisionQueryParams& TraceParams) const
{
	if (InitialMaxDist <= 15.0f)
	{
		return InitialMaxDist;
	}

	// Krok próbkowania wzdłuż promienia: 35 cm.
	// Zapewnia ciągłość powierzchni i uniemożliwia przeskakiwanie nad szczelinami między filarami.
	constexpr float StepDist = 35.0f;
	float LastValidDist = 0.0f;
	float FirstInvalidDist = -1.0f;

	// 1. Sekwencyjny marsz od środka na zewnątrz (Ray Marching)
	for (float CurrentDist = StepDist; CurrentDist < InitialMaxDist; CurrentDist += StepDist)
	{
		if (CheckSurfacePresentAt(Center + RayDir * CurrentDist, LiftOffset, TraceParams))
		{
			LastValidDist = CurrentDist;
		}
		else
		{
			FirstInvalidDist = CurrentDist;
			break;
		}
	}

	// Jeśli żaden krok pośredni nie natrafił na pustkę, sprawdzamy punkt końcowy
	if (FirstInvalidDist < 0.0f)
	{
		if (CheckSurfacePresentAt(Center + RayDir * InitialMaxDist, LiftOffset, TraceParams))
		{
			return InitialMaxDist;
		}
		FirstInvalidDist = InitialMaxDist;
	}

	// 2. Lokalny binary search w przedziale [LastValidDist, FirstInvalidDist] dla dokładności co do centymetra
	float Low = LastValidDist;
	float High = FirstInvalidDist;
	for (int32 Step = 0; Step < 3; ++Step)
	{
		const float Mid = (Low + High) * 0.5f;
		if (CheckSurfacePresentAt(Center + RayDir * Mid, LiftOffset, TraceParams))
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}

	return Low;
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

	constexpr float LiftOffset = 25.0f;
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

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const float AngleRad = FMath::DegreesToRadians(static_cast<float>(i) * (360.0f / static_cast<float>(NumSegments)));
		const FVector RayDir = (FMath::Cos(AngleRad) * AxisY + FMath::Sin(AngleRad) * AxisZ).GetSafeNormal();

		// 1. Weryfikacja przeszkód wyrastających z powierzchni (ściany, filary, progi)
		float AllowedDist = TraceObstacleDistance(Center, TraceStart, RayDir, Radius, TraceParams);

		// 2. Weryfikacja obecności podłoża (Edge & Drop-Off Detection)
		AllowedDist = FindDropOffEdgeDistance(Center, RayDir, AllowedDist, LiftOffset, TraceParams);

		CachedPerimeterPoints.Add(Center + RayDir * AllowedDist);
		CachedPerimeterDistances.Add(AllowedDist);
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

bool ASurfaceSplashZone::IsWithinNormalBounds(const FBoxSphereBounds& Bounds, float& OutDistNormal) const
{
	const FVector ZoneCenter = GetActorLocation();
	const FVector Extent = Bounds.BoxExtent;

	const float ProjectedHalfExtent = FMath::Abs(SurfaceNormal.X) * Extent.X +
	                                  FMath::Abs(SurfaceNormal.Y) * Extent.Y +
	                                  FMath::Abs(SurfaceNormal.Z) * Extent.Z;

	OutDistNormal = FVector::DotProduct(Bounds.Origin - ZoneCenter, SurfaceNormal);
	const float MinDist = OutDistNormal - ProjectedHalfExtent;
	const float MaxDist = OutDistNormal + ProjectedHalfExtent;

	// Obiekt znajduje się głęboko pod płaszczyzną powierzchni (np. w podłodze)
	if (MaxDist < -15.0f)
	{
		return false;
	}

	// Obiekt znajduje się ponad dopuszczalną grubością strefy (+15 cm buforu na tolerancję fizyki Chaos i nierówności)
	if (MinDist > GetMaxAllowedHeight() + 15.0f)
	{
		return false;
	}

	return true;
}

bool ASurfaceSplashZone::IsWithinTangentialPerimeter(const FBoxSphereBounds& Bounds, float DistNormal) const
{
	const FVector ZoneCenter = GetActorLocation();
	const FVector ToBounds = Bounds.Origin - ZoneCenter;
	const FVector TangentialVec = ToBounds - DistNormal * SurfaceNormal;
	const float TangentialDist = TangentialVec.Size();

	if (TangentialDist <= 1.0f)
	{
		return true;
	}

	const FVector Extent = Bounds.BoxExtent;
	const FVector TangentialDir = TangentialVec / TangentialDist;
	const float TangentialExtent = FMath::Abs(TangentialDir.X) * Extent.X +
	                               FMath::Abs(TangentialDir.Y) * Extent.Y +
	                               FMath::Abs(TangentialDir.Z) * Extent.Z;

	const FMatrix SurfaceMatrix = FRotationMatrix::MakeFromX(SurfaceNormal);
	const FVector AxisY = SurfaceMatrix.GetScaledAxis(EAxis::Y);
	const FVector AxisZ = SurfaceMatrix.GetScaledAxis(EAxis::Z);

	const float AngleY = FVector::DotProduct(TangentialDir, AxisY);
	const float AngleZ = FVector::DotProduct(TangentialDir, AxisZ);
	const float AngleRad = FMath::Atan2(AngleZ, AngleY);

	const float AllowedRadius = GetPerimeterRadiusAtAngle(AngleRad);

	// Bufor 15 cm odpowiadający rzeczywistemu zasięgowi dekalów i krawędzi obiektów
	return (TangentialDist - TangentialExtent <= AllowedRadius + 15.0f);
}

bool ASurfaceSplashZone::IsActorWithinZoneGeometry(const FBoxSphereBounds& Bounds) const
{
	if (!IsPerimeterCacheValid())
	{
		const_cast<ASurfaceSplashZone*>(this)->RebuildPerimeterPoints();
	}

	float DistNormal = 0.0f;
	if (!IsWithinNormalBounds(Bounds, DistNormal))
	{
		return false;
	}

	return IsWithinTangentialPerimeter(Bounds, DistNormal);
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
		if (IsCoplanarWith(OtherSplash))
		{
			const float DistSq = FVector::DistSquared(GetActorLocation(), OtherSplash->GetActorLocation());
			return DistSq <= FMath::Square(Radius + OtherSplash->GetRadius());
		}

		// Różne/prostopadłe płaszczyzny (np. podłoga i ściana)
		const float DistToOtherPlane = FMath::Abs(FVector::DotProduct(GetActorLocation() - OtherSplash->GetActorLocation(), OtherSplash->GetSurfaceNormal()));
		const float DistToThisPlane = FMath::Abs(FVector::DotProduct(OtherSplash->GetActorLocation() - GetActorLocation(), SurfaceNormal));

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
		return IsCoplanarWith(OtherSplash);
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
	const float DebugLifeTime = ZoneTickInterval + 0.05f;

	if (NumPoints >= 3 && MaxDistSq >= 2500.0f)
	{
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const FVector Pt1 = CachedPerimeterPoints[i] + VisualOffset;
			const FVector Pt2 = CachedPerimeterPoints[(i + 1) % NumPoints] + VisualOffset;
			DrawDebugLine(GetWorld(), Pt1, Pt2, ZoneColor, false, DebugLifeTime, 0, 4.0f);
		}
	}
	else
	{
		FMatrix Matrix = FRotationMatrix::MakeFromX(SurfaceNormal);
		Matrix.SetOrigin(Center + VisualOffset);
		DrawDebugCircle(GetWorld(), Matrix, Radius, 48, ZoneColor, false, DebugLifeTime, 0, 4.0f, false);
	}

	const FVector TextPos = Center + SurfaceNormal * 30.0f;
	DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, DebugLifeTime, true, 1.2f);
#endif
}

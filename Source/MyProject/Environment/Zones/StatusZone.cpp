#include "StatusZone.h"

#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"

AStatusZone::AStatusZone()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(false);

	ZoneCollision = CreateDefaultSubobject<USphereComponent>(TEXT("ZoneCollision"));
	RootComponent = ZoneCollision;

	ZoneCollision->SetSphereRadius(Radius);
	ZoneCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	ZoneCollision->SetGenerateOverlapEvents(true);
	ZoneCollision->CanCharacterStepUpOn = ECB_No;

	ZoneDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("ZoneDecal"));
	ZoneDecal->SetupAttachment(RootComponent);
	ZoneDecal->DecalSize = FVector(50.0f, Radius, Radius);
	ZoneDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));

	Radius = 300.0f;
	SurfaceHeight = 35.0f;
	SurfaceNormal = FVector::UpVector;
	ShapeType = EZoneShapeType::SurfaceSplash;
	ZoneCreationTime = 0.0f;
	bDrawDebugZone = true;
}

void AStatusZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStatusZone, EffectConfig);
	DOREPLIFETIME(AStatusZone, Radius);
	DOREPLIFETIME(AStatusZone, SurfaceHeight);
	DOREPLIFETIME(AStatusZone, ServerEndTime);
	DOREPLIFETIME(AStatusZone, SurfaceNormal);
	DOREPLIFETIME(AStatusZone, ShapeType);
	DOREPLIFETIME(AStatusZone, ZoneCreationTime);
}

void AStatusZone::BeginPlay()
{
	Super::BeginPlay();

	if (ZoneCollision)
	{
		ZoneCollision->OnComponentBeginOverlap.AddDynamic(this, &AStatusZone::HandleBeginOverlap);
	}

	RebuildPerimeterPoints();
}

void AStatusZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AStatusZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GetWorld())
	{
		return;
	}

	if (HasAuthority())
	{
		const float CurrentTime = GetWorld()->GetTimeSeconds();

		if (ServerEndTime > 0.0f && CurrentTime >= ServerEndTime)
		{
			Destroy();
			return;
		}

		// Niezawodne, bezstanowe sprawdzanie obecności co 0.25s (dokładnie jak w ElementalStatusZone)
		if (CurrentTime - LastTickTime >= 0.25f)
		{
			LastTickTime = CurrentTime;
			ProcessActiveOverlaps();
		}
	}

	DrawDebugVisuals();
}

void AStatusZone::InitializeZone(
	const FZoneEffectConfig& InConfig,
	float InRadius,
	float InDuration,
	EZoneShapeType InShapeType,
	float InSurfaceHeight,
	const FVector& InSurfaceNormal,
	AActor* InInstigator)
{
	REQUIRE_AUTHORITY();

	if (!GetWorld()) return;

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	EffectConfig = InConfig;
	Radius = FMath::Max(30.0f, InRadius);
	SurfaceHeight = FMath::Clamp(InSurfaceHeight, 10.0f, 120.0f);
	ServerEndTime = (InDuration > 0.0f) ? (CurrentTime + InDuration) : 0.0f;
	ZoneCreationTime = CurrentTime;
	ShapeType = InShapeType;
	SurfaceNormal = InSurfaceNormal.IsNearlyZero() ? FVector::UpVector : InSurfaceNormal.GetSafeNormal();
	ZoneInstigator = InInstigator;

	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(Radius);
	}

	if (ZoneDecal)
	{
		if (ShapeType == EZoneShapeType::SurfaceSplash)
		{
			ZoneDecal->SetVisibility(true);
			const float ProjectionDepth = FMath::Max(SurfaceHeight * 2.0f, 60.0f);
			ZoneDecal->DecalSize = FVector(ProjectionDepth, Radius, Radius);
			ZoneDecal->SetWorldRotation(FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator());
		}
		else
		{
			ZoneDecal->SetVisibility(false);
		}
	}

	RebuildPerimeterPoints();
	ForceNetUpdate();
	ProcessActiveOverlaps();
}

void AStatusZone::RebuildPerimeterPoints()
{
	CachedPerimeterPoints.Reset();

	if (!GetWorld() || Radius <= 0.0f || ShapeType != EZoneShapeType::SurfaceSplash)
	{
		return;
	}

	const FVector Center = GetActorLocation();
	const FMatrix SurfaceMatrix = FRotationMatrix::MakeFromX(SurfaceNormal);
	const FVector AxisY = SurfaceMatrix.GetScaledAxis(EAxis::Y);
	const FVector AxisZ = SurfaceMatrix.GetScaledAxis(EAxis::Z);

	// Uniesienie 25 cm nad posadzkę - dokładnie jak w ElementalStatusZone
	const float LiftOffset = 25.0f;
	const FVector TraceStart = Center + SurfaceNormal * LiftOffset;

	constexpr int32 NumSegments = 48;
	CachedPerimeterPoints.Reserve(NumSegments);

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
		const FVector TraceEnd = TraceStart + RayDir * Radius;

		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
		{
			if (Hit.bStartPenetrating || Hit.Distance < 20.0f)
			{
				FHitResult InwardHit;
				if (GetWorld()->LineTraceSingleByChannel(InwardHit, TraceEnd, TraceStart, ECC_WorldStatic, TraceParams))
				{
					const bool bInwardIsWall = FMath::Abs(FVector::DotProduct(InwardHit.ImpactNormal, SurfaceNormal)) < 0.6f;
					if (bInwardIsWall)
					{
						CachedPerimeterPoints.Add(InwardHit.ImpactPoint - SurfaceNormal * (LiftOffset - 4.0f));
					}
					else
					{
						CachedPerimeterPoints.Add(TraceEnd - SurfaceNormal * (LiftOffset - 4.0f));
					}
				}
				else
				{
					CachedPerimeterPoints.Add(TraceEnd - SurfaceNormal * (LiftOffset - 4.0f));
				}
				continue;
			}

			const bool bIsWall = FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) < 0.6f;
			if (bIsWall)
			{
				CachedPerimeterPoints.Add(Hit.ImpactPoint - SurfaceNormal * (LiftOffset - 4.0f));
			}
			else
			{
				CachedPerimeterPoints.Add(TraceEnd - SurfaceNormal * (LiftOffset - 4.0f));
			}
		}
		else
		{
			CachedPerimeterPoints.Add(TraceEnd - SurfaceNormal * (LiftOffset - 4.0f));
		}
	}
}

void AStatusZone::DrawDebugVisuals() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDrawDebugZone || !GetWorld())
	{
		return;
	}

	const float Remaining = FMath::Max(0.0f, ServerEndTime - GetWorld()->GetTimeSeconds());

	FColor ZoneColor = FColor::White;
	FString StatusName = TEXT("ZONE");
	switch (EffectConfig.AppliedStatus)
	{
	case EStatusEffectType::Burning:
		ZoneColor = FColor(255, 60, 0);
		StatusName = TEXT("BURNING ZONE");
		break;
	case EStatusEffectType::Wet:
		ZoneColor = FColor(0, 180, 255);
		StatusName = TEXT("WATER ZONE");
		break;
	case EStatusEffectType::Electrified:
		ZoneColor = FColor(255, 230, 0);
		StatusName = TEXT("ELECTRIFIED ZONE");
		break;
	case EStatusEffectType::Oiled:
		ZoneColor = FColor(180, 110, 40);
		StatusName = TEXT("OIL ZONE");
		break;
	default:
		if (EffectConfig.MovementSpeedMultiplier < 0.99f)
		{
			ZoneColor = FColor(130, 70, 180);
			StatusName = TEXT("SLOW ZONE");
		}
		break;
	}

	const FVector Center = GetActorLocation();

	if (ShapeType == EZoneShapeType::SurfaceSplash)
	{
		if (CachedPerimeterPoints.Num() < 3)
		{
			const_cast<AStatusZone*>(this)->RebuildPerimeterPoints();
		}

		float MaxDistSq = 0.0f;
		for (const FVector& Pt : CachedPerimeterPoints)
		{
			MaxDistSq = FMath::Max(MaxDistSq, FVector::DistSquared(Pt, Center));
		}

		const int32 NumPoints = CachedPerimeterPoints.Num();
		if (NumPoints >= 3 && MaxDistSq >= 2500.0f)
		{
			for (int32 i = 0; i < NumPoints; ++i)
			{
				const FVector& Pt1 = CachedPerimeterPoints[i];
				const FVector& Pt2 = CachedPerimeterPoints[(i + 1) % NumPoints];
				DrawDebugLine(GetWorld(), Pt1, Pt2, ZoneColor, false, 0.0f, 0, 4.0f);
			}
		}
		else
		{
			FMatrix Matrix = FRotationMatrix::MakeFromX(SurfaceNormal);
			Matrix.SetOrigin(Center + SurfaceNormal * 4.0f);
			DrawDebugCircle(GetWorld(), Matrix, Radius, 48, ZoneColor, false, 0.0f, 0, 4.0f, false);
		}

		const FVector TextPos = Center + SurfaceNormal * 30.0f;
		DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, 0.0f, true, 1.2f);
	}
	else
	{
		DrawDebugSphere(GetWorld(), Center, Radius, 16, ZoneColor, false, 0.0f, 0, 2.0f);
		const FVector TextPos = Center + FVector(0.0f, 0.0f, 30.0f);
		DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, 0.0f, true, 1.2f);
	}
#endif
}

void AStatusZone::OnRep_EffectConfig()
{
}

void AStatusZone::OnRep_Radius()
{
	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(Radius);
	}
	if (ZoneDecal && ShapeType == EZoneShapeType::SurfaceSplash)
	{
		ZoneDecal->DecalSize = FVector(SurfaceHeight * 2.0f, Radius, Radius);
	}
	RebuildPerimeterPoints();
}

void AStatusZone::OnRep_SurfaceHeight()
{
	if (ZoneDecal && ShapeType == EZoneShapeType::SurfaceSplash)
	{
		ZoneDecal->DecalSize = FVector(SurfaceHeight * 2.0f, Radius, Radius);
	}
}

void AStatusZone::OnRep_ServerEndTime()
{
	// Klienci nie niszczą strefy ręcznie, tylko serwer zarządza replikacją aktora
}

void AStatusZone::OnRep_SurfaceNormal()
{
	if (ZoneDecal && ShapeType == EZoneShapeType::SurfaceSplash)
	{
		ZoneDecal->SetWorldRotation(FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator());
	}
	RebuildPerimeterPoints();
}

void AStatusZone::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	REQUIRE_AUTHORITY();

	if (!OtherActor || OtherActor == this) return;

	// Reakcja między dwiema strefami
	if (AStatusZone* OtherZone = Cast<AStatusZone>(OtherActor))
	{
		OtherZone->ApplyElementalHit(EffectConfig.AppliedStatus, 0.0f, this);
		return;
	}

	if (!IsActorEligibleForZoneEffect(OtherActor, OtherComp)) return;

	if (EffectConfig.AppliedStatus != EStatusEffectType::None)
	{
		if (UStatusEffectComponent* StatusComp = OtherActor->FindComponentByClass<UStatusEffectComponent>())
		{
			StatusComp->ApplyStatus(EffectConfig.AppliedStatus, 3.0f, ZoneInstigator.Get());
		}
	}
}

void AStatusZone::ProcessActiveOverlaps()
{
	REQUIRE_AUTHORITY();

	if (!GetWorld() || !ZoneCollision) return;

	TArray<AActor*> OverlappingActors;
	ZoneCollision->GetOverlappingActors(OverlappingActors);

	for (AActor* Actor : OverlappingActors)
	{
		if (!Actor || Actor == this) continue;

		if (AStatusZone* OtherZone = Cast<AStatusZone>(Actor))
		{
			OtherZone->ApplyElementalHit(EffectConfig.AppliedStatus, 0.0f, this);
			continue;
		}

		if (!IsActorEligibleForZoneEffect(Actor, nullptr)) continue;

		// 1. Status żywiołowy - odświeżany co 0.25s
		if (EffectConfig.AppliedStatus != EStatusEffectType::None)
		{
			if (UStatusEffectComponent* StatusComp = Actor->FindComponentByClass<UStatusEffectComponent>())
			{
				StatusComp->ApplyStatus(EffectConfig.AppliedStatus, 2.5f, ZoneInstigator.Get());
			}
		}

		// 2. Obrażenia ciągłe DoT
		if (EffectConfig.ContinuousDamagePerSec > 0.0f)
		{
			if (UDamageableComponent* DmgComp = Actor->FindComponentByClass<UDamageableComponent>())
			{
				DmgComp->ApplyDamage(EffectConfig.ContinuousDamagePerSec * 0.25f);
			}
		}
	}
}

bool AStatusZone::IsActorEligibleForZoneEffect(AActor* TargetActor, UPrimitiveComponent* TargetComp) const
{
	if (!TargetActor || TargetActor == this || !GetWorld()) return false;

	const FVector ZoneCenter = GetActorLocation();

	// 1. Ochrona przed przenikaniem przez ściany i lotem w powietrzu (dokładnie jak w ElementalStatusZone)
	if (ShapeType == EZoneShapeType::SurfaceSplash)
	{
		FBoxSphereBounds Bounds;
		if (TargetComp)
		{
			Bounds = TargetComp->Bounds;
		}
		else if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(TargetActor->GetRootComponent()))
		{
			Bounds = RootPrim->Bounds;
		}
		else
		{
			Bounds = TargetActor->GetComponentsBoundingBox(true);
		}

		const FVector BoundsOrigin = Bounds.BoxExtent.IsNearlyZero() ? TargetActor->GetActorLocation() : Bounds.Origin;
		const FVector Extent = Bounds.BoxExtent;

		// Rzut połowy wymiarów bryły na wektor normalny powierzchni
		const float ProjectedHalfExtent = FMath::Abs(SurfaceNormal.X) * Extent.X +
		                                  FMath::Abs(SurfaceNormal.Y) * Extent.Y +
		                                  FMath::Abs(SurfaceNormal.Z) * Extent.Z;

		const float DistCenter = FVector::DotProduct(BoundsOrigin - ZoneCenter, SurfaceNormal);
		const float MinDist = DistCenter - ProjectedHalfExtent;
		const float MaxDist = DistCenter + ProjectedHalfExtent;

		// Cel znajduje się całkowicie za ścianą lub pod posadzką
		if (MaxDist < -15.0f)
		{
			return false;
		}

		// Cel znajduje się w całości powyżej powierzchni (np. lecący w powietrzu, skaczący gracz)
		const float MaxAllowedHeight = (EffectConfig.AppliedStatus == EStatusEffectType::Burning) ? 85.0f : SurfaceHeight;
		if (MinDist > MaxAllowedHeight)
		{
			return false;
		}
	}

	// 2. Weryfikacja geometryczna Line of Sight (przeszkody architektoniczne, filary)
	FHitResult LoSHit;
	const AActor* IgnoredActor = ZoneInstigator.IsValid() ? ZoneInstigator.Get() : this;
	if (!UKineticForceLibrary::HasExplosionLineOfSight(GetWorld(), ZoneCenter, TargetActor, TargetComp, LoSHit, IgnoredActor))
	{
		return false;
	}

	// 3. Rozwiązywanie konfliktów nakładających się płynów
	if (IsOverruledByNewerLiquidZone(TargetActor->GetActorLocation()))
	{
		return false;
	}

	return true;
}

bool AStatusZone::IsOverruledByNewerLiquidZone(const FVector& TargetLocation) const
{
	if (!UElementalChemistryLibrary::IsLiquidStatus(EffectConfig.AppliedStatus) || !GetWorld() || !ZoneCollision)
	{
		return false;
	}

	TArray<AActor*> OverlappingZones;
	ZoneCollision->GetOverlappingActors(OverlappingZones, AStatusZone::StaticClass());

	for (AActor* Actor : OverlappingZones)
	{
		if (const AStatusZone* OtherZone = Cast<AStatusZone>(Actor))
		{
			if (OtherZone == this) continue;

			if (UElementalChemistryLibrary::IsLiquidStatus(OtherZone->GetStatusType()) &&
				OtherZone->GetZoneCreationTime() > ZoneCreationTime)
			{
				const float DistSq = FVector::DistSquared(TargetLocation, OtherZone->GetActorLocation());
				if (DistSq <= FMath::Square(OtherZone->GetRadius()))
				{
					return true;
				}
			}
		}
	}

	return false;
}

void AStatusZone::ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage, AActor* HitInstigator)
{
	REQUIRE_AUTHORITY();

	if (IncomingStatus == EStatusEffectType::None || !GetWorld()) return;

	if (UElementalChemistryLibrary::IsLiquidStatus(IncomingStatus) && UElementalChemistryLibrary::IsLiquidStatus(EffectConfig.AppliedStatus))
	{
		if (HitInstigator && HitInstigator != this)
		{
			const float Dist = FVector::Dist(GetActorLocation(), HitInstigator->GetActorLocation());
			if (Dist <= Radius * 0.6f)
			{
				Destroy();
				return;
			}
		}
	}

	const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(IncomingStatus, { EffectConfig.AppliedStatus });
	if (Reaction.bReactionOccurred)
	{
		const EStatusEffectType OldStatus = EffectConfig.AppliedStatus;

		if (Reaction.ReactionTag == FName(TEXT("Oil_Ignition")))
		{
			EffectConfig.AppliedStatus = EStatusEffectType::Burning;
			EffectConfig.ContinuousDamagePerSec = 15.0f;
			EffectConfig.MovementSpeedMultiplier = 1.0f;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 10.0f;

			OnZoneReaction.Broadcast(OldStatus, EffectConfig.AppliedStatus);
			ForceNetUpdate();

			UKineticForceLibrary::ApplyExplosion(this, GetActorLocation(), Radius, 25.0f, 1200.0f, this, nullptr, false);
			ProcessActiveOverlaps();
			return;
		}

		if (Reaction.ReactionTag == FName(TEXT("Steam_Extinguish")) || Reaction.ReactionTag == FName(TEXT("Fire_Extinguished")))
		{
			EffectConfig.AppliedStatus = EStatusEffectType::Wet;
			EffectConfig.ContinuousDamagePerSec = 0.0f;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 8.0f;

			OnZoneReaction.Broadcast(OldStatus, EffectConfig.AppliedStatus);
			ForceNetUpdate();
			ProcessActiveOverlaps();
			return;
		}

		if (Reaction.ReactionTag == FName(TEXT("Conductive_Shock")))
		{
			EffectConfig.AppliedStatus = EStatusEffectType::Electrified;
			EffectConfig.ContinuousDamagePerSec = 5.0f;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 6.0f;

			OnZoneReaction.Broadcast(OldStatus, EffectConfig.AppliedStatus);
			ForceNetUpdate();
			ProcessActiveOverlaps();
			return;
		}
	}
}

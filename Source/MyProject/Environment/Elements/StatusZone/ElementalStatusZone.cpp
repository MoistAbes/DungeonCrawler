#include "ElementalStatusZone.h"

#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Dungeon/Structure/DungeonStructureBase.h"

AElementalStatusZone::AElementalStatusZone()
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

	StatusType = EStatusEffectType::None;
	Radius = 300.0f;
	BurnDamagePerSecond = 10.0f;
	bDrawDebugZone = true;
	SurfaceNormal = FVector::UpVector;
	ShapeMode = EStatusZoneShapeMode::SurfaceDisk;
	ZoneCreationTime = 0.0f;
}

void AElementalStatusZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AElementalStatusZone, StatusType);
	DOREPLIFETIME(AElementalStatusZone, Radius);
	DOREPLIFETIME(AElementalStatusZone, ServerEndTime);
	DOREPLIFETIME(AElementalStatusZone, SurfaceNormal);
	DOREPLIFETIME(AElementalStatusZone, ShapeMode);
	DOREPLIFETIME(AElementalStatusZone, ZoneCreationTime);
}

void AElementalStatusZone::BeginPlay()
{
	Super::BeginPlay();

	if (ZoneCollision)
	{
		ZoneCollision->OnComponentBeginOverlap.AddDynamic(this, &AElementalStatusZone::HandleBeginOverlap);
	}

	RebuildPerimeterPoints();
}

void AElementalStatusZone::InitializeZone(
	EStatusEffectType InStatus,
	float InRadius,
	float InDuration,
	EStatusZoneShapeMode InShapeMode,
	const FVector& InSurfaceNormal,
	AActor* InInstigator)
{
	REQUIRE_AUTHORITY();

	if (!GetWorld() || InStatus == EStatusEffectType::None)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	StatusType = InStatus;
	Radius = FMath::Max(50.0f, InRadius);
	ServerEndTime = CurrentTime + InDuration;
	ZoneCreationTime = CurrentTime;
	ShapeMode = InShapeMode;
	SurfaceNormal = InSurfaceNormal.IsNearlyZero() ? FVector::UpVector : InSurfaceNormal.GetSafeNormal();
	ZoneInstigator = InInstigator;

	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(Radius);
	}

	if (ZoneDecal)
	{
		const float ProjectionDepth = (ShapeMode == EStatusZoneShapeMode::SurfaceDisk) ? 60.0f : Radius;
		ZoneDecal->DecalSize = FVector(ProjectionDepth, Radius, Radius);
		ZoneDecal->SetWorldRotation(FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator());
	}

	RebuildPerimeterPoints();
	ForceNetUpdate();
	ProcessActiveOverlaps();
}

void AElementalStatusZone::RebuildPerimeterPoints()
{
	CachedPerimeterPoints.Reset();

	if (!GetWorld() || Radius <= 0.0f || ShapeMode != EStatusZoneShapeMode::SurfaceDisk)
	{
		return;
	}

	const FVector Center = GetActorLocation();
	const FMatrix SurfaceMatrix = FRotationMatrix::MakeFromX(SurfaceNormal);
	const FVector AxisY = SurfaceMatrix.GetScaledAxis(EAxis::Y);
	const FVector AxisZ = SurfaceMatrix.GetScaledAxis(EAxis::Z);

	// Podnosimy promień testowy o 25 cm nad powierzchnię, aby nie zahaczał o szwy kafelków podłogowych
	const float LiftOffset = 25.0f;
	const FVector TraceStart = Center + SurfaceNormal * LiftOffset;

	constexpr int32 NumSegments = 48;
	CachedPerimeterPoints.Reserve(NumSegments);

	// Ignorujemy samą strefę oraz prop wybuchający, a badamy TYLKO geometrię statyczną architektury (ECC_WorldStatic)
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
		// Badamy tylko architekturę poziomów (ściany, filary, portale), ignorując postacie, graczy i propy
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
		{
			// Jeśli środek wybuchu dotyka bezpośrednio ściany (start penetrating lub trafienie w punkcie zerowym),
			// sprawdzamy ścianę od zewnątrz do wewnątrz, by precyzyjnie znaleźć front ściany
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

			// Sprawdzamy czy trafiliśmy w ścianę (powierzchnia prostopadła do podłogi)
			const bool bIsWall = FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal)) < 0.6f;
			if (bIsWall)
			{
				CachedPerimeterPoints.Add(Hit.ImpactPoint - SurfaceNormal * (LiftOffset - 4.0f));
			}
			else
			{
				// Trafienie w podłogę/sufit ignorujemy – strefa rozlewa się do pełnego promienia
				CachedPerimeterPoints.Add(TraceEnd - SurfaceNormal * (LiftOffset - 4.0f));
			}
		}
		else
		{
			// Otwarta przestrzeń – pełny okrąg strefy
			CachedPerimeterPoints.Add(TraceEnd - SurfaceNormal * (LiftOffset - 4.0f));
		}
	}
}

void AElementalStatusZone::ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage, AActor* HitInstigator)
{
	REQUIRE_AUTHORITY();

	if (IncomingStatus == EStatusEffectType::None || !GetWorld())
	{
		return;
	}

	// 1. Bezpośrednie trafienie płynem w środek innego płynu (np. beczka wody rzucona w plamę oleju)
	if (UElementalChemistryLibrary::IsLiquidStatus(IncomingStatus) && UElementalChemistryLibrary::IsLiquidStatus(StatusType))
	{
		if (HitInstigator && HitInstigator != this)
		{
			const float Dist = FVector::Dist(GetActorLocation(), HitInstigator->GetActorLocation());
			// Jeśli środek nowej plamy wylądował blisko środka starej - nowy płyn zmywa stary
			if (Dist <= Radius * 0.6f)
			{
				Destroy();
				return;
			}
		}
	}

	// 2. Reakcje chemiczne
	const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(IncomingStatus, { StatusType });
	if (Reaction.bReactionOccurred)
	{
		const EStatusEffectType OldStatus = StatusType;

		// Zapłon oleju: cała strefa natychmiast staje w płomieniach
		if (Reaction.ReactionTag == FName(TEXT("Oil_Ignition")))
		{
			StatusType = EStatusEffectType::Burning;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 10.0f;
			OnZoneReaction.Broadcast(OldStatus, StatusType);
			ForceNetUpdate();

			UKineticForceLibrary::ApplyExplosion(this, GetActorLocation(), Radius, 25.0f, 1200.0f, this, nullptr, false);
			ProcessActiveOverlaps();
			return;
		}

		// Ugaszenie ognia przez wodę: strefa staje się mokra (wspiera uderzenie wody w ogień jak i ognia w wodę)
		if (Reaction.ReactionTag == FName(TEXT("Steam_Extinguish")) || Reaction.ReactionTag == FName(TEXT("Fire_Extinguished")))
		{
			StatusType = EStatusEffectType::Wet;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 8.0f;
			OnZoneReaction.Broadcast(OldStatus, StatusType);
			ForceNetUpdate();
			ProcessActiveOverlaps();
			return;
		}

		// Elektryzacja: woda staje się naelektryzowana (Conductive_Shock z rejestru definicji)
		if (Reaction.ReactionTag == FName(TEXT("Conductive_Shock")))
		{
			StatusType = EStatusEffectType::Electrified;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 6.0f;
			OnZoneReaction.Broadcast(OldStatus, StatusType);
			ForceNetUpdate();
			ProcessActiveOverlaps();
			return;
		}
	}
}

bool AElementalStatusZone::IsOverruledByNewerLiquidZone(const FVector& TargetLocation) const
{
	if (!UElementalChemistryLibrary::IsLiquidStatus(StatusType) || !GetWorld() || !ZoneCollision)
	{
		return false;
	}

	TArray<AActor*> OverlappingZones;
	ZoneCollision->GetOverlappingActors(OverlappingZones, AElementalStatusZone::StaticClass());

	for (AActor* Actor : OverlappingZones)
	{
		if (const AElementalStatusZone* OtherZone = Cast<AElementalStatusZone>(Actor))
		{
			if (OtherZone == this)
			{
				continue;
			}

			// Jeśli inna strefa też jest płynem i jest MŁODSZA (powstała później niż ta)
			if (UElementalChemistryLibrary::IsLiquidStatus(OtherZone->GetStatusType()) &&
				OtherZone->GetZoneCreationTime() > ZoneCreationTime)
			{
				const float DistSq = FVector::DistSquared(TargetLocation, OtherZone->GetActorLocation());
				if (DistSq <= FMath::Square(OtherZone->GetRadius()))
				{
					// Nowszy płyn ma pierwszeństwo – ta starsza strefa nie nadpisuje celu
					return true;
				}
			}
		}
	}

	return false;
}

bool AElementalStatusZone::IsActorEligibleForZoneEffect(AActor* TargetActor, UPrimitiveComponent* TargetComp) const
{
	if (!TargetActor || TargetActor == this || !GetWorld())
	{
		return false;
	}

	const FVector ZoneCenter = GetActorLocation();

	// 1. Ochrona przed przenikaniem przez ściany w trybie powierzchniowym (Half-Space Check)
	if (ShapeMode == EStatusZoneShapeMode::SurfaceDisk)
	{
		const FVector TargetLoc = TargetActor->GetActorLocation();
		const float Projection = FVector::DotProduct(TargetLoc - ZoneCenter, SurfaceNormal);
		if (Projection < -15.0f)
		{
			return false;
		}
	}

	// 2. Weryfikacja geometryczna Line of Sight (przeszkody architektoniczne, filary)
	FHitResult LoSHit;
	if (!UKineticForceLibrary::HasExplosionLineOfSight(GetWorld(), ZoneCenter, TargetActor, TargetComp, LoSHit, this))
	{
		return false;
	}

	// 3. Rozwiązywanie konfliktów nakładających się płynów:
	// Jeśli obiekt znajduje się w zasięgu NOWSZEJ strefy płynu, ta starsza strefa ustępuje pierwszeństwa
	if (IsOverruledByNewerLiquidZone(TargetActor->GetActorLocation()))
	{
		return false;
	}

	return true;
}

void AElementalStatusZone::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	REQUIRE_AUTHORITY();

	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	// Reakcja między dwoma strefami
	if (AElementalStatusZone* OtherZone = Cast<AElementalStatusZone>(OtherActor))
	{
		OtherZone->ApplyElementalHit(StatusType, 0.0f, this);
		return;
	}

	if (!IsActorEligibleForZoneEffect(OtherActor, OtherComp))
	{
		return;
	}

	if (UStatusEffectComponent* StatusComp = OtherActor->FindComponentByClass<UStatusEffectComponent>())
	{
		StatusComp->ApplyStatus(StatusType, 3.0f, ZoneInstigator.Get());
	}
}

void AElementalStatusZone::ProcessActiveOverlaps()
{
	REQUIRE_AUTHORITY();

	if (!GetWorld() || !ZoneCollision)
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	ZoneCollision->GetOverlappingActors(OverlappingActors);

	for (AActor* Actor : OverlappingActors)
	{
		if (!Actor || Actor == this)
		{
			continue;
		}

		if (AElementalStatusZone* OtherZone = Cast<AElementalStatusZone>(Actor))
		{
			OtherZone->ApplyElementalHit(StatusType, 0.0f, this);
			continue;
		}

		if (!IsActorEligibleForZoneEffect(Actor, nullptr))
		{
			continue;
		}

		if (UStatusEffectComponent* StatusComp = Actor->FindComponentByClass<UStatusEffectComponent>())
		{
			StatusComp->ApplyStatus(StatusType, 2.5f, ZoneInstigator.Get());
		}

		// Obrażenia pożaru dla niszczalnych drewnianych ścian i struktur (kamień/metal nie ulegają spaleniu)
		if (StatusType == EStatusEffectType::Burning)
		{
			if (ADungeonStructureBase* Structure = Cast<ADungeonStructureBase>(Actor))
			{
				if (Structure->IsDestructible() && Structure->GetDamageableComponent())
				{
					if (Structure->GetMaterialType_Implementation() == EPhysicalMaterialType::Wood)
					{
						Structure->GetDamageableComponent()->ApplyDamage(BurnDamagePerSecond);
					}
				}
			}
		}
	}
}

void AElementalStatusZone::Tick(float DeltaTime)
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

		if (CurrentTime - LastTickTime >= 0.75f)
		{
			LastTickTime = CurrentTime;
			ProcessActiveOverlaps();
		}
	}

	DrawDebugVisuals();
}

void AElementalStatusZone::OnRep_StatusType()
{
}

void AElementalStatusZone::OnRep_Radius()
{
	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(Radius);
	}
	if (ZoneDecal)
	{
		const float ProjectionDepth = (ShapeMode == EStatusZoneShapeMode::SurfaceDisk) ? 60.0f : Radius;
		ZoneDecal->DecalSize = FVector(ProjectionDepth, Radius, Radius);
	}
	RebuildPerimeterPoints();
}

void AElementalStatusZone::OnRep_ServerEndTime()
{
}

void AElementalStatusZone::OnRep_SurfaceNormal()
{
	if (ZoneDecal)
	{
		ZoneDecal->SetWorldRotation(FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator());
	}
	RebuildPerimeterPoints();
}

void AElementalStatusZone::DrawDebugVisuals() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDrawDebugZone || !GetWorld())
	{
		return;
	}

	const float Remaining = FMath::Max(0.0f, ServerEndTime - GetWorld()->GetTimeSeconds());

	FColor ZoneColor = FColor::White;
	FString StatusName = TEXT("ZONE");
	switch (StatusType)
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
		break;
	}

	const FVector Center = GetActorLocation();

	if (ShapeMode == EStatusZoneShapeMode::SurfaceDisk)
	{
		if (CachedPerimeterPoints.Num() < 3)
		{
			const_cast<AElementalStatusZone*>(this)->RebuildPerimeterPoints();
		}

		// Weryfikacja czy punkty nie zapadły się do środka
		float MaxDistSq = 0.0f;
		for (const FVector& Pt : CachedPerimeterPoints)
		{
			MaxDistSq = FMath::Max(MaxDistSq, FVector::DistSquared(Pt, Center));
		}

		const int32 NumPoints = CachedPerimeterPoints.Num();
		if (NumPoints >= 3 && MaxDistSq >= 2500.0f) // Minimum 50 cm rozpiętości
		{
			// Rysujemy obrys geometryczny zatrzymujący się na ścianach
			for (int32 i = 0; i < NumPoints; ++i)
			{
				const FVector& Pt1 = CachedPerimeterPoints[i];
				const FVector& Pt2 = CachedPerimeterPoints[(i + 1) % NumPoints];
				DrawDebugLine(GetWorld(), Pt1, Pt2, ZoneColor, false, 0.0f, 0, 4.0f);
			}
		}
		else
		{
			// Gwarantowany fallback do czystego okręgu
			FMatrix Matrix = FRotationMatrix::MakeFromX(SurfaceNormal);
			Matrix.SetOrigin(Center + SurfaceNormal * 4.0f);
			DrawDebugCircle(GetWorld(), Matrix, Radius, 48, ZoneColor, false, 0.0f, 0, 4.0f, false);
		}

		// Zegar odliczający odsunięty w stronę pokoju
		const FVector TextPos = Center + SurfaceNormal * 30.0f;
		DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, 0.0f, true, 1.2f);
	}
	else
	{
		// Objętościowa strefa sferyczna
		DrawDebugSphere(GetWorld(), Center, Radius, 16, ZoneColor, false, 0.0f, 0, 2.0f);
		const FVector TextPos = Center + FVector(0.0f, 0.0f, 30.0f);
		DrawDebugString(GetWorld(), TextPos, FString::Printf(TEXT("[%s: %.1fs | R: %.0f cm]"), *StatusName, Remaining, Radius), nullptr, ZoneColor, 0.0f, true, 1.2f);
	}
#endif
}

#include "StatusZoneBase.h"

#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include "MyProject/Networking/NetworkFunctionLibrary.h"
#include "MyProject/Logging/DungeonLogCategories.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"
#include "MyProject/Environment/Elements/Utilities/ElementalChemistryLibrary.h"
#include "MyProject/Shared/Components/StatusEffectComponent/StatusEffectComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"

AStatusZoneBase::AStatusZoneBase()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(false);

	Radius = 300.0f;
	ShapeType = EZoneShapeType::SurfaceSplash;
	ZoneCreationTime = 0.0f;
	bDrawDebugZone = true;

	ZoneCollision = CreateDefaultSubobject<USphereComponent>(TEXT("ZoneCollision"));
	RootComponent = ZoneCollision;

	ZoneCollision->SetSphereRadius(CalculateBroadphaseRadius());
	ZoneCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	ZoneCollision->SetGenerateOverlapEvents(true);
	ZoneCollision->CanCharacterStepUpOn = ECB_No;
}

void AStatusZoneBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStatusZoneBase, EffectConfig);
	DOREPLIFETIME(AStatusZoneBase, Radius);
	DOREPLIFETIME(AStatusZoneBase, ServerEndTime);
	DOREPLIFETIME(AStatusZoneBase, ShapeType);
	DOREPLIFETIME(AStatusZoneBase, ZoneCreationTime);
}

void AStatusZoneBase::BeginPlay()
{
	Super::BeginPlay();

	if (ZoneCollision)
	{
		ZoneCollision->OnComponentBeginOverlap.AddDynamic(this, &AStatusZoneBase::HandleBeginOverlap);
	}
}

void AStatusZoneBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AStatusZoneBase::Tick(float DeltaTime)
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

		// Niezawodne, bezstanowe sprawdzanie obecności co 0.25s
		if (CurrentTime - LastTickTime >= 0.25f)
		{
			LastTickTime = CurrentTime;
			ProcessActiveOverlaps();
		}
	}

	DrawDebugVisuals();
}

void AStatusZoneBase::InitializeZoneBase(
	const FZoneEffectConfig& InConfig,
	float InRadius,
	float InDuration,
	EZoneShapeType InShapeType,
	AActor* InInstigator)
{
	REQUIRE_AUTHORITY();

	if (!GetWorld()) return;

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	EffectConfig = InConfig;
	Radius = FMath::Max(30.0f, InRadius);
	ServerEndTime = (InDuration > 0.0f) ? (CurrentTime + InDuration) : 0.0f;
	ZoneCreationTime = CurrentTime;
	ShapeType = InShapeType;
	ZoneInstigator = InInstigator;

	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(CalculateBroadphaseRadius());
	}

	ForceNetUpdate();
	ProcessActiveOverlaps();
}

void AStatusZoneBase::OnRep_EffectConfig()
{
}

void AStatusZoneBase::OnRep_Radius()
{
	if (ZoneCollision)
	{
		ZoneCollision->SetSphereRadius(CalculateBroadphaseRadius());
	}
}

void AStatusZoneBase::OnRep_ServerEndTime()
{
}

void AStatusZoneBase::HandleBeginOverlap(
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
	if (AStatusZoneBase* OtherZone = Cast<AStatusZoneBase>(OtherActor))
	{
		if (CanZonesInteract(OtherZone))
		{
			OtherZone->ApplyElementalHit(EffectConfig.AppliedStatus, 0.0f, this);
		}
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

void AStatusZoneBase::ProcessActiveOverlaps()
{
	REQUIRE_AUTHORITY();

	if (!GetWorld() || !ZoneCollision) return;

	TArray<AActor*> OverlappingActors;
	ZoneCollision->GetOverlappingActors(OverlappingActors);

	for (AActor* Actor : OverlappingActors)
	{
		if (!Actor || Actor == this) continue;

		if (AStatusZoneBase* OtherZone = Cast<AStatusZoneBase>(Actor))
		{
			if (CanZonesInteract(OtherZone))
			{
				OtherZone->ApplyElementalHit(EffectConfig.AppliedStatus, 0.0f, this);
			}
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

bool AStatusZoneBase::IsActorEligibleForZoneEffect(AActor* TargetActor, UPrimitiveComponent* TargetComp) const
{
	if (!TargetActor || TargetActor == this || !GetWorld()) return false;

	const FVector ZoneCenter = GetActorLocation();

	// 1. Weryfikacja geometrii powłoki / bryły (delegacja do klasy potomnej)
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

	if (Bounds.BoxExtent.IsNearlyZero())
	{
		Bounds.Origin = TargetActor->GetActorLocation();
		Bounds.BoxExtent = FVector(34.0f, 34.0f, 88.0f);
	}

	if (!IsActorWithinZoneGeometry(Bounds))
	{
		return false;
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

bool AStatusZoneBase::IsOverruledByNewerLiquidZone(const FVector& TargetLocation) const
{
	if (!UElementalChemistryLibrary::IsLiquidStatus(EffectConfig.AppliedStatus) || !GetWorld() || !ZoneCollision)
	{
		return false;
	}

	TArray<AActor*> OverlappingZones;
	ZoneCollision->GetOverlappingActors(OverlappingZones, AStatusZoneBase::StaticClass());

	for (AActor* Actor : OverlappingZones)
	{
		if (const AStatusZoneBase* OtherZone = Cast<AStatusZoneBase>(Actor))
		{
			if (OtherZone == this) continue;

			// Nadpisywanie dotyczy WYŁĄCZNIE różnych płynów (np. nowy olej na starą wodę)
			if (OtherZone->GetStatusType() != EffectConfig.AppliedStatus &&
				UElementalChemistryLibrary::IsLiquidStatus(OtherZone->GetStatusType()) &&
				OtherZone->GetZoneCreationTime() > ZoneCreationTime)
			{
				FBoxSphereBounds PointBounds;
				PointBounds.Origin = TargetLocation;
				PointBounds.BoxExtent = FVector(10.0f, 10.0f, 10.0f);
				if (OtherZone->IsActorWithinZoneGeometry(PointBounds))
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool AStatusZoneBase::CanZonesInteract(const AStatusZoneBase* OtherZone) const
{
	if (!OtherZone || OtherZone == this)
	{
		return false;
	}

	// Strefy tego samego żywiołu nie wywołują reakcji żywiołowych
	if (OtherZone->GetStatusType() == EffectConfig.AppliedStatus)
	{
		return false;
	}

	return true;
}

bool AStatusZoneBase::HandleLiquidDisplacement(AActor* HitInstigator)
{
	return true;
}

void AStatusZoneBase::MergeWithZone(float InDuration, float RadiusGrowthMultiplier, float MaxRadiusCap)
{
	REQUIRE_AUTHORITY();

	if (!GetWorld())
	{
		return;
	}

	// 1. Odświeżenie / wydłużenie czasu trwania
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	const float NewEndTime = CurrentTime + InDuration;
	ServerEndTime = FMath::Max(ServerEndTime, NewEndTime);

	// 2. Bezpieczne i kontrolowane powiększenie promienia
	const float TargetRadius = FMath::Min(Radius * RadiusGrowthMultiplier, MaxRadiusCap);
	if (TargetRadius > Radius)
	{
		Radius = TargetRadius;
		OnRep_Radius();
	}
}

void AStatusZoneBase::ApplyElementalHit(EStatusEffectType IncomingStatus, float InstantDamage, AActor* HitInstigator)
{
	REQUIRE_AUTHORITY();

	if (IncomingStatus == EStatusEffectType::None || !GetWorld()) return;

	// Ten sam żywioł nie wywołuje reakcji żywiołowych
	if (IncomingStatus == EffectConfig.AppliedStatus) return;

	const FElementalReactionResult Reaction = UElementalChemistryLibrary::EvaluateReaction(IncomingStatus, { EffectConfig.AppliedStatus });
	if (Reaction.bReactionOccurred)
	{
		const EStatusEffectType OldStatus = EffectConfig.AppliedStatus;

		// Wypieranie powłok płynnych (np. woda wypiera olej, olej wypiera wodę)
		if (Reaction.ReactionTag == FName(TEXT("Liquid_Displaced")))
		{
			if (HandleLiquidDisplacement(HitInstigator))
			{
				Destroy();
				return;
			}
			return;
		}

		if (Reaction.ReactionTag == FName(TEXT("Oil_Ignition")))
		{
			EffectConfig.AppliedStatus = EStatusEffectType::Burning;
			EffectConfig.ContinuousDamagePerSec = 15.0f;
			ServerEndTime = GetWorld()->GetTimeSeconds() + 10.0f;

			if (ZoneCollision)
			{
				ZoneCollision->SetSphereRadius(CalculateBroadphaseRadius());
			}

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

FColor AStatusZoneBase::GetStatusDebugColor() const
{
	switch (EffectConfig.AppliedStatus)
	{
	case EStatusEffectType::Burning:
		return FColor(255, 60, 0);
	case EStatusEffectType::Wet:
		return FColor(0, 180, 255);
	case EStatusEffectType::Electrified:
		return FColor(255, 230, 0);
	case EStatusEffectType::Oiled:
		return FColor(180, 110, 40);
	default:
		return FColor::White;
	}
}

FString AStatusZoneBase::GetStatusDebugName() const
{
	switch (EffectConfig.AppliedStatus)
	{
	case EStatusEffectType::Burning:
		return TEXT("BURNING ZONE");
	case EStatusEffectType::Wet:
		return TEXT("WATER ZONE");
	case EStatusEffectType::Electrified:
		return TEXT("ELECTRIFIED ZONE");
	case EStatusEffectType::Oiled:
		return TEXT("OIL ZONE");
	default:
		return TEXT("ZONE");
	}
}

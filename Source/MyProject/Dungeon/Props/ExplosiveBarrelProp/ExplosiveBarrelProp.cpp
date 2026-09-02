#include "ExplosiveBarrelProp.h"

#include "Components/StaticMeshComponent.h"
#include "MyProject/Shared/Components/DamageableComponent/DamageableComponent.h"
#include "MyProject/Environment/Kinetic/Utilities/KineticForceLibrary.h"

AExplosiveBarrelProp::AExplosiveBarrelProp()
{
	MaterialType = EPhysicalMaterialType::Wood;
	bCanBeGrabbed = true;

	// Beczka jest łatwa do zdetonowania: mały próg uderzenia
	ExplosionRadius = 600.0f;
	ExplosionDamage = 25.0f;
	ExplosionKnockbackForce = 1800.0f;
	bDrawDebugExplosion = true;
}

void AExplosiveBarrelProp::HandleOnDestroyed(AActor* DestroyedActor)
{
	if (bHasExploded)
	{
		return;
	}
	bHasExploded = true;

	const FVector ExplosionCenter = GetActorLocation();

	// Wywołujemy silną eksplozję zadającą obrażenia i odrzut w promieniu
	UKineticForceLibrary::ApplyExplosion(
		this,
		ExplosionCenter,
		ExplosionRadius,
		ExplosionDamage,
		ExplosionKnockbackForce,
		this,
		nullptr,
		bDrawDebugExplosion);

	UE_LOG(LogTemp, Warning, TEXT("[ExplosiveBarrel] BOOM! Detonated at %s"), *ExplosionCenter.ToString());

	Super::HandleOnDestroyed(DestroyedActor);
}

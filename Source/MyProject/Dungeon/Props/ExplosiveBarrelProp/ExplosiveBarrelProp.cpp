#include "ExplosiveBarrelProp.h"

AExplosiveBarrelProp::AExplosiveBarrelProp()
{
	MaterialType = EPhysicalMaterialType::Wood;
	bCanBeGrabbed = true;

	EffectRadius = 600.0f;
	BaseDamage = 25.0f;
	KnockbackForce = 1800.0f;
	bApplyKnockback = true;
	StatusToApply = EStatusEffectType::Burning;
	StatusDuration = 6.0f;
	bDrawDebugRadius = true;
}

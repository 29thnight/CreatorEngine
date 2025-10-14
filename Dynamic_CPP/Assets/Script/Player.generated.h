#pragma once

#define ReflectPlayer \
ReflectionScriptField(Player) \
{ \
	PropertyField \
	({ \
		meta_property(playerIndex) \
		meta_property(m_playerType) \
		meta_property(moveSpeed) \
		meta_property(chargingMoveSpeed) \
		meta_property(maxHP) \
		meta_property(ThrowPowerX) \
		meta_property(ThrowPowerY) \
		meta_property(DropPowerX) \
		meta_property(DropPowerY) \
		meta_property(detectAngle) \
		meta_property(dashDistacne) \
		meta_property(m_dashTime) \
		meta_property(dashCooldown) \
		meta_property(dashGracePeriod) \
		meta_property(dashAmount) \
		meta_property(dubbleDashTime) \
		meta_property(Atk) \
		meta_property(comboDuration) \
		meta_property(rangedAutoAimRange) \
		meta_property(rangeAngle) \
		meta_property(countSpecialBullet) \
		meta_property(bombMoveSpeed) \
		meta_property(slash1Offset) \
		meta_property(slash2Offset) \
		meta_property(slashChargeOffset) \
		meta_property(MultipleAttackSpeed) \
		meta_property(stunRespawnTime) \
		meta_property(HitGracePeriodTime) \
		meta_property(ResurrectionRange) \
		meta_property(ResurrectionTime) \
		meta_property(ResurrectionHP) \
		meta_property(ResurrectionGracePeriod) \
		meta_property(SlotChangeCooldown) \
	}); \
	MethodField \
	({ \
		meta_method(SetCurHP, "hp") \
		meta_method(Damage, "damage") \
		meta_method(SwapWeaponLeft) \
		meta_method(SwapWeaponRight) \
		meta_method(SwapBasicWeapon) \
		meta_method(AddMeleeWeapon) \
		meta_method(DeleteCurWeapon) \
		meta_method(Move, "dir") \
		meta_method(PlaySoundStep) \
		meta_method(CatchAndThrow) \
		meta_method(ThrowEvent) \
		meta_method(Dash) \
		meta_method(PlaySoundDash) \
		meta_method(Cancancel) \
		meta_method(MeleeChargeAttack) \
		meta_method(ShootBullet) \
		meta_method(ShootNormalBullet) \
		meta_method(ShootSpecialBullet) \
		meta_method(ThrowBomb) \
		meta_method(StartAttack) \
		meta_method(Charging) \
		meta_method(ChargeAttack) \
		meta_method(StartRay) \
		meta_method(EndRay) \
		meta_method(EndAttack) \
		meta_method(PlaySlashEvent) \
		meta_method(PlaySlashEvent2) \
		meta_method(PlaySlashEvent3) \
	}); \
	FieldEnd(Player, PropertyAndMethod) \
};

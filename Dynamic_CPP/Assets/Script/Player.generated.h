#pragma once

#define ReflectPlayer \
ReflectionScriptField(Player) \
{ \
	PropertyField \
	({ \
		meta_property(playerIndex) \
		meta_property(moveSpeed) \
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
		meta_property(AttackRange) \
		meta_property(AttackSpeed) \
		meta_property(comboDuration) \
		meta_property(atkFwDistacne) \
		meta_property(rangedAtkCountMax) \
		meta_property(rangedAtkDelay) \
		meta_property(rangedAtkCooldown) \
		meta_property(rangedAutoAimRange) \
		meta_property(minChargedTime) \
		meta_property(GracePeriod) \
		meta_property(ResurrectionRange) \
		meta_property(ResurrectionTime) \
		meta_property(ResurrectionHP) \
		meta_property(ResurrectionGracePeriod) \
		meta_property(SlotChangeCooldown) \
	}); \
	MethodField \
	({ \
		meta_method(SwapWeaponLeft) \
		meta_method(SwapWeaponRight) \
		meta_method(DeleteCurWeapon) \
		meta_method(Move, "dir") \
		meta_method(CatchAndThrow) \
		meta_method(ThrowEvent) \
		meta_method(Dash) \
		meta_method(ShootBullet) \
		meta_method(StartAttack) \
		meta_method(Charging) \
		meta_method(Attack1) \
		meta_method(StartRay) \
		meta_method(EndRay) \
		meta_method(OnBuff) \
	}); \
	FieldEnd(Player, PropertyAndMethod) \
};

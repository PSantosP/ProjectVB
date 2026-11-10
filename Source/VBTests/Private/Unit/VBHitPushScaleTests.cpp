#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBHitImpact.h"
#include "AbilitySystem/Abilities/VBGA_HitReact.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Data/VBSwingCombatProfile.h"
#include "Data/VBAttackConfig.h"
#include "Combat/VBProjectileConfig.h"
#include "GameplayEffect.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 루트모션 이동 배율과 밀치기 거리 추정을 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitPushScaleTest, "VB.Unit.HitReact.RootMotionPushScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitPushScaleTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("Ground push braking upper bound"), VBEstimateKnockbackDistance(204.0f, 0.0f, 980.0f, 2048.0f), 10.16f, 0.1f);
	TestEqual(TEXT("No strength, no distance"), VBEstimateKnockbackDistance(0.0f, 0.0f, 980.0f, 2048.0f), 0.0f);
	TestEqual(TEXT("NaN strength, no distance"), VBEstimateKnockbackDistance(NAN, 0.0f, 980.0f, 2048.0f), 0.0f);
	TestNearlyEqual(TEXT("No gravity drops explicit flight"), VBEstimateKnockbackDistance(204.0f, 200.0f, 0.0f, 2048.0f), 10.16f, 0.1f);
	TestNearlyEqual(TEXT("Launch height adds flight distance"), VBEstimateKnockbackDistance(100.0f, 200.0f, 980.0f, 2048.0f),
		130.21f, 0.1f);
	TestEqual(TEXT("No braking or flight yields no estimate"), VBEstimateKnockbackDistance(200.0f, 0.0f, 980.0f, 0.0f), 0.0f);

	// 배율: 바닥 1, 상한 MaxScale.
	TestNearlyEqual(TEXT("60cm over a 40cm clip is x1.5"), UVBGA_HitReact::ComputeRootMotionPushScale(60.0f, 40.0f, 3.0f), 1.5f, 1e-4f);
	TestEqual(TEXT("Weaker than the clip keeps the clip (floor 1)"), UVBGA_HitReact::ComputeRootMotionPushScale(21.0f, 40.0f, 3.0f), 1.0f);
	TestEqual(TEXT("Capped at MaxScale"), UVBGA_HitReact::ComputeRootMotionPushScale(400.0f, 40.0f, 3.0f), 3.0f);
	TestEqual(TEXT("No desired distance leaves the clip"), UVBGA_HitReact::ComputeRootMotionPushScale(0.0f, 40.0f, 3.0f), 1.0f);
	TestEqual(TEXT("A clip that does not move is not scaled"), UVBGA_HitReact::ComputeRootMotionPushScale(60.0f, 0.5f, 3.0f), 1.0f);
	TestEqual(TEXT("MaxScale below 1 is read as 1"), UVBGA_HitReact::ComputeRootMotionPushScale(60.0f, 40.0f, 0.5f), 1.0f);

	// 실어 나르기: 방향과 거리가 한 끝점 변환에 산다. 거리를 실어도 방향이 안 바뀐다.
	FGameplayEventData WithYaw;
	VBSetPushYaw(WithYaw, 30.0f);
	VBSetPushDistance(WithYaw, 95.0f);
	float Yaw = 0.0f, Distance = 0.0f;
	TestTrue(TEXT("Yaw survives the distance"), VBGetPushYaw(WithYaw, Yaw) && FMath::IsNearlyEqual(Yaw, 30.0f, 1e-3f));
	TestTrue(TEXT("Distance reads back"), VBGetPushDistance(WithYaw, Distance) && FMath::IsNearlyEqual(Distance, 95.0f, 1e-3f));
	FGameplayEventData NoYaw;
	VBSetPushDistance(NoYaw, 95.0f);
	TestFalse(TEXT("No push direction, no distance"), VBGetPushDistance(NoYaw, Distance));
	FGameplayEventData YawOnly;
	VBSetPushYaw(YawOnly, 30.0f);
	TestFalse(TEXT("Direction alone carries no distance"), VBGetPushDistance(YawOnly, Distance));
	return true;
}

// 일반 밀치기와 명시적 띄우기의 계약을 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitMovementTest, "VB.Unit.HitReact.Movement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitMovementTest::RunTest(const FString& Parameters)
{
	const FVector Forward = FVector::ForwardVector;
	const FVBHitMovement Push = VBResolveHitMovement(Forward, 100.0f, false, 0.0f, 980.0f);
	TestTrue(TEXT("Ordinary hit is a planar push"), Push.Mode == EVBHitMovement::Push && Push.Velocity.Equals(FVector(100.0, 0.0, 0.0)));
	const FVBHitMovement Tilted = VBResolveHitMovement(FVector(1.0, 0.0, 1.0), 100.0f, false, 0.0f, 980.0f);
	TestTrue(TEXT("Push direction never introduces vertical velocity"), Tilted.Velocity.Equals(Push.Velocity));
	const FVBHitMovement Launch = VBResolveHitMovement(Forward, 300.0f, false, 200.0f, 980.0f);
	TestTrue(TEXT("Authored height selects launch"), Launch.Mode == EVBHitMovement::Launch);
	TestNearlyEqual(TEXT("200 cm launch vertical speed"), static_cast<float>(Launch.Velocity.Z), 626.1f, 0.1f);
	TestNearlyEqual(TEXT("Launch keeps authored horizontal speed"), static_cast<float>(Launch.Velocity.X), 300.0f, 0.01f);
	TestTrue(TEXT("Root motion owns ordinary translation"),
		VBResolveHitMovement(Forward, 100.0f, true, 0.0f, 980.0f).Mode == EVBHitMovement::None);
	const FVBHitMovement ClipLaunch = VBResolveHitMovement(Forward, 100.0f, true, 200.0f, 980.0f);
	TestTrue(TEXT("Root motion can still launch vertically"), ClipLaunch.Mode == EVBHitMovement::Launch
		&& FMath::IsNearlyZero(ClipLaunch.Velocity.SizeSquared2D()));
	TestTrue(TEXT("No strength leaves motion unchanged"),
		VBResolveHitMovement(Forward, 0.0f, false, 0.0f, 980.0f).Mode == EVBHitMovement::None);
	TestTrue(TEXT("NaN strength leaves motion unchanged"),
		VBResolveHitMovement(Forward, NAN, false, 0.0f, 980.0f).Mode == EVBHitMovement::None);
	TestTrue(TEXT("No direction leaves motion unchanged"),
		VBResolveHitMovement(FVector::ZeroVector, 100.0f, false, 0.0f, 980.0f).Mode == EVBHitMovement::None);
	TestTrue(TEXT("No gravity cannot launch"), VBResolveHitMovement(Forward, 0.0f, false, 200.0f, 0.0f).Mode == EVBHitMovement::None);
	TestTrue(TEXT("Infinite height cannot launch"),
		VBResolveHitMovement(Forward, 0.0f, false, INFINITY, 980.0f).Mode == EVBHitMovement::None);
	FVBSwingCombatScales Scales;
	TestFalse(TEXT("Default scales have no effect"), Scales.HasAnyEffect());
	Scales.PoiseDamageScale = 0.345f;
	TestTrue(TEXT("Poise scale alone is an effect"), Scales.HasAnyEffect());
	return true;
}

#if WITH_EDITOR
// 폐기된 상방 밀치기 데이터가 다시 들어오면 검증에서 거부한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLegacyHitPopValidationTest, "VB.Unit.HitReact.LegacyPopValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLegacyHitPopValidationTest::RunTest(const FString& Parameters)
{
	UVBAttackConfig* Attack = NewObject<UVBAttackConfig>();
	UVBProjectileConfig* Projectile = NewObject<UVBProjectileConfig>();
	Projectile->DamageEffectClass = UGameplayEffect::StaticClass();
	for (UObject* Asset : { static_cast<UObject*>(Attack), static_cast<UObject*>(Projectile) })
	{
		FDataValidationContext Clean;
		TestTrue(TEXT("Default data has no legacy pop"), Asset->IsDataValid(Clean) != EDataValidationResult::Invalid);
		if (Asset == Attack)
		{
			Attack->KnockbackZ = 120.0f;
		}
		else
		{
			Projectile->KnockbackZ = 120.0f;
		}

		FDataValidationContext Legacy;
		TestTrue(TEXT("Old 120 cm/s pop data fails validation"), Asset->IsDataValid(Legacy) == EDataValidationResult::Invalid);
	}

	UVBSwingCombatProfile* Profile = NewObject<UVBSwingCombatProfile>();
	Profile->bOverrideHitImpact = true;
	FDataValidationContext Clean;
	TestTrue(TEXT("Default legacy scale is valid"), Profile->IsDataValid(Clean) != EDataValidationResult::Invalid);
	Profile->Scales.KnockbackZScale = 2.0f;
	FDataValidationContext Legacy;
	TestTrue(TEXT("Old pop multiplier fails validation"), Profile->IsDataValid(Legacy) == EDataValidationResult::Invalid);
	return true;
}
#endif

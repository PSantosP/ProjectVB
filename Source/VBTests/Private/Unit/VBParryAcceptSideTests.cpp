// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Data/VBParryConfig.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
// 받아넘김은 공격이 온 쪽으로 고른다: 오른쪽이면 오른쪽 클립(있을 때), 그 밖은 기본 클립.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBParryAcceptSideTest, "VB.Unit.Character.ParryAcceptSide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBParryAcceptSideTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UAnimMontage> Left(NewObject<UAnimMontage>());
	const TStrongObjectPtr<UAnimMontage> Right(NewObject<UAnimMontage>());
	FVBGuardSet Set;
	Set.ParryAcceptMontage = Left.Get();
	TestTrue(TEXT("no right clip: right side keeps the default"), Set.PickParryAccept(EVBHitDirection::Right) == Left.Get());
	Set.ParryAcceptMontageRight = Right.Get();
	TestTrue(TEXT("right side plays the right clip"), Set.PickParryAccept(EVBHitDirection::Right) == Right.Get());
	for (const EVBHitDirection Side : {EVBHitDirection::Front, EVBHitDirection::Back, EVBHitDirection::Left})
	{
		TestTrue(TEXT("every other side plays the default"), Set.PickParryAccept(Side) == Left.Get());
	}

	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/VBSlideMovementComponent.h"
#include "Data/VBMovementConfig.h"
#include "Animation/VBAnimationTypes.h"

// 가드 중 이동: 걷기 속도로 묶이고, 걷기까지 덮는 세트만 움직이는 동안 가드 하체를 쓴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardMovementTest, "VB.Unit.Character.GuardMovement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBGuardMovementTest::RunTest(const FString& Parameters)
{
	FVBMovementProfile Profile;
	Profile.WalkSpeeds = FVector(150.0, 140.0, 130.0);
	Profile.RunSpeeds = FVector(400.0, 380.0, 360.0);
	Profile.SprintRunSpeeds = FVector(600.0, 600.0, 600.0);
	Profile.SprintSpeeds = FVector(700.0, 700.0, 700.0);
	TestEqual(TEXT("guarding while sprinting walks"),
		UVBSlideMovementComponent::PickGaitSpeeds(Profile, true, true, true, EVBGait::Run), Profile.WalkSpeeds);
	TestEqual(TEXT("guarding while running walks"),
		UVBSlideMovementComponent::PickGaitSpeeds(Profile, true, false, false, EVBGait::Run), Profile.WalkSpeeds);
	TestEqual(TEXT("not guarding: run stays run"),
		UVBSlideMovementComponent::PickGaitSpeeds(Profile, false, false, false, EVBGait::Run), Profile.RunSpeeds);

	FVBGuardSet Standing; // Blade, 대검: 가드 걷기 없음
	TestFalse(TEXT("guarding with a weapon without guard walk: no clamp (can still run)"),
		UVBSlideMovementComponent::ShouldClampGuardToWalk(true, &Standing));
	TestFalse(TEXT("no weapon in hand: no clamp"), UVBSlideMovementComponent::ShouldClampGuardToWalk(true, nullptr));
	TestTrue(TEXT("still: guard lower body allowed"), Standing.AllowsGuardLowerBody(false));
	TestFalse(TEXT("moving without walk clips: full-body guard"), Standing.AllowsGuardLowerBody(true));
	FVBGuardSet Walking;
	Walking.bGuardLocomotionCoversMovement = true;
	TestTrue(TEXT("moving with walk clips: guard lower body"), Walking.AllowsGuardLowerBody(true));
	TestTrue(TEXT("guarding with guard walk: walk speeds"), UVBSlideMovementComponent::ShouldClampGuardToWalk(true, &Walking));
	TestFalse(TEXT("not guarding: no clamp"), UVBSlideMovementComponent::ShouldClampGuardToWalk(false, &Walking));
	return true;
}
#endif

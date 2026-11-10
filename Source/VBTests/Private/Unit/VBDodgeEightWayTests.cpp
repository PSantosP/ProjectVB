// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Abilities/VBGA_Dodge.h"
#include "Animation/AnimMontage.h"
#include "Character/VBSlideMovementComponent.h"
#include "Data/VBDodgeConfig.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
// 8방향 판정, 대각선이 없으면 4방향 칸, 질주 중 구르기, 앞쪽으로 계속 밀 때만 달리기로 이어 가기.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDodgeEightWayTest, "VB.Unit.Dodge.EightWay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBDodgeEightWayTest::RunTest(const FString& Parameters)
{
	auto Eight = [](float X, float Y) { return UVBGA_Dodge::HeadingFromLocal(FVector2D(X, Y)).Eight; };
	TestTrue(TEXT("forward"), Eight(1, 0) == EVBDodgeDirection::Forward);
	TestTrue(TEXT("forward right"), Eight(1, 1) == EVBDodgeDirection::ForwardRight);
	TestTrue(TEXT("right"), Eight(0, 1) == EVBDodgeDirection::Right);
	TestTrue(TEXT("backward right"), Eight(-1, 1) == EVBDodgeDirection::BackwardRight);
	TestTrue(TEXT("backward"), Eight(-1, 0) == EVBDodgeDirection::Backward);
	TestTrue(TEXT("backward left"), Eight(-1, -1) == EVBDodgeDirection::BackwardLeft);
	TestTrue(TEXT("left"), Eight(0, -1) == EVBDodgeDirection::Left);
	TestTrue(TEXT("forward left"), Eight(1, -1) == EVBDodgeDirection::ForwardLeft);
	TestTrue(TEXT("20 degrees stays forward"),
		Eight(FMath::Cos(FMath::DegreesToRadians(20.f)), FMath::Sin(FMath::DegreesToRadians(20.f))) == EVBDodgeDirection::Forward);
	TestTrue(TEXT("still = backward (old fallback)"), Eight(0, 0) == EVBDodgeDirection::Backward);
	TestTrue(TEXT("cardinal keeps the old 4-way rule"),
		UVBGA_Dodge::HeadingFromLocal(FVector2D(1, 0.9f)).Cardinal == EVBDodgeDirection::Forward);

	const TStrongObjectPtr<UAnimMontage> Fwd(NewObject<UAnimMontage>());
	const TStrongObjectPtr<UAnimMontage> FwdRight(NewObject<UAnimMontage>());
	FVBDodgeMontageSet FourWay;
	FourWay.Forward = Fwd.Get();
	const FVBDodgeHeading Diagonal = UVBGA_Dodge::HeadingFromLocal(FVector2D(1, 0.9f));
	TestTrue(TEXT("no diagonal clip: the cardinal clip (today's behaviour)"), UVBDodgeConfig::PickFromSet(FourWay, Diagonal) == Fwd.Get());
	FourWay.ForwardRight = FwdRight.Get();
	TestTrue(TEXT("diagonal clip wins when authored"), UVBDodgeConfig::PickFromSet(FourWay, Diagonal) == FwdRight.Get());

	TestTrue(TEXT("sprinting with a roll clip rolls"), UVBGA_Dodge::PickStepOrRoll(Fwd.Get(), FwdRight.Get(), true) == FwdRight.Get());
	TestTrue(TEXT("not sprinting steps"), UVBGA_Dodge::PickStepOrRoll(Fwd.Get(), FwdRight.Get(), false) == Fwd.Get());
	TestTrue(TEXT("sprinting without a roll clip steps"), UVBGA_Dodge::PickStepOrRoll(Fwd.Get(), nullptr, true) == Fwd.Get());

	const FVector DodgeDir(1, 0, 0);
	TestTrue(TEXT("stick held toward the dodge hands over"),
		UVBSlideMovementComponent::ShouldHandOverToRun(FVector(1, 0.3f, 0), DodgeDir, 60.f));
	TestFalse(TEXT("stick sideways does not"), UVBSlideMovementComponent::ShouldHandOverToRun(FVector(0, 1, 0), DodgeDir, 60.f));
	TestFalse(TEXT("no stick does not"), UVBSlideMovementComponent::ShouldHandOverToRun(FVector::ZeroVector, DodgeDir, 60.f));
	return true;
}
#endif

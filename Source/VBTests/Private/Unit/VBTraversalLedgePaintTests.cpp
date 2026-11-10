// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Traversal/VBLevelBlock_Traversable.h"

// 넘기 블록 표시 띠의 자리 - 윗면 네 모서리 안쪽과 그 아래 옆면. 틀리면 띠가 공중에 뜨거나 상자 속에 묻힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalLedgePaintTest, "VB.Unit.Traversal.LedgePaintPlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTraversalLedgePaintTest::RunTest(const FString& Parameters)
{
	const FBox Box(FVector(-50, -20, 0), FVector(50, 20, 100));
	TArray<FTransform> Out;
	AVBLevelBlock_Traversable::ComputeLedgePaintTransforms(Box, 6.0f, 5.0f, Out);
	if (!TestEqual(TEXT("four top strips and four face bands"), Out.Num(), 8)) { return false; }

	const FTransform& TopFront = Out[0];
	TestTrue(TEXT("top strip sits just above the top face"), FMath::IsNearlyEqual(TopFront.GetLocation().Z, 100.2, 0.01));
	TestTrue(TEXT("top strip is inset by half its width from the edge"), FMath::IsNearlyEqual(TopFront.GetLocation().Y, 17.0, 0.01));
	TestTrue(TEXT("top strip spans the whole edge"), FMath::IsNearlyEqual(TopFront.GetScale3D().X, 1.0, 0.001));
	TestTrue(TEXT("top strip is as wide as the setting"), FMath::IsNearlyEqual(TopFront.GetScale3D().Y, 0.06, 0.001));
	const FTransform& SideStrip = Out[3];
	TestTrue(TEXT("short-side top strip runs along Y"), FMath::IsNearlyEqual(SideStrip.GetScale3D().Y, 0.4, 0.001) && FMath::IsNearlyEqual(SideStrip.GetLocation().X, -47.0, 0.01));

	const FTransform& FaceFront = Out[4];
	TestTrue(TEXT("face band hangs below the top edge"), FMath::IsNearlyEqual(FaceFront.GetLocation().Z, 97.5, 0.01));
	TestTrue(TEXT("face band sits just outside the face"), FMath::IsNearlyEqual(FaceFront.GetLocation().Y, 20.2, 0.01));
	TestTrue(TEXT("face band is as tall as the setting"), FMath::IsNearlyEqual(FaceFront.GetScale3D().Z, 0.05, 0.001));

	AVBLevelBlock_Traversable::ComputeLedgePaintTransforms(Box, 6.0f, 0.0f, Out);
	TestEqual(TEXT("no face height draws only the top strips"), Out.Num(), 4);
	return true;
}
#endif

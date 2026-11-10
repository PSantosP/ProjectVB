// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBSummonedWeapon.h"
#include "Data/VBWeaponVisualConfig.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Unit/VBIsolatedGameWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

// 소환 무기는 평소 틱을 쓰지 않는다. 미리보기가 틱을 켜고, 끝나면 스스로 끈다.
//   액터는 BeginPlay 전까지 틱이 꺼져 있어 시작 값은 CDO 의 설정으로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSummonedWeaponPreviewTickTest, "VB.Unit.Weapon.SummonedPreviewTick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSummonedWeaponPreviewTickTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	const AVBSummonedWeapon* Defaults = GetDefault<AVBSummonedWeapon>();
	TestTrue(TEXT("the actor can tick for the preview"), Defaults->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("the actor does not start ticking"), Defaults->PrimaryActorTick.bStartWithTickEnabled);

	AVBSummonedWeapon* Weapon = Isolated.World->SpawnActor<AVBSummonedWeapon>();
	if (!TestNotNull(TEXT("summoned weapon"), Weapon))
	{
		return false;
	}

	Weapon->VisualPart.PreviewDuration = 2.0f;
	Weapon->PreviewSummon();
	TestTrue(TEXT("the preview turns tick on"), Weapon->IsActorTickEnabled());
	Weapon->Tick(1.0f);
	TestEqual(TEXT("progress follows the part's preview duration"), Weapon->ManifestProgress, 0.5f);
	TestTrue(TEXT("tick stays on until the preview ends"), Weapon->IsActorTickEnabled());
	Weapon->Tick(1.0f);
	TestEqual(TEXT("the preview finishes"), Weapon->ManifestProgress, 1.0f);
	TestFalse(TEXT("the finished preview turns tick off"), Weapon->IsActorTickEnabled());
	return true;
}

// 조정값으로 옮긴 숫자는 옮기기 전 코드의 값이어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponVisualPartDefaultsTest, "VB.Unit.Weapon.VisualPartDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponVisualPartDefaultsTest::RunTest(const FString& Parameters)
{
	const FVBWeaponVisualPart Part;
	TestEqual(TEXT("reveal front scale"), Part.RevealFrontScale, 1.10f);
	TestEqual(TEXT("reveal front bias"), Part.RevealFrontBias, 0.05f);
	TestEqual(TEXT("formation color scale"), Part.FormationColorScale, 1.8f);
	TestEqual(TEXT("formation secondary color scale"), Part.FormationSecondaryColorScale, 0.6f);
	TestEqual(TEXT("idle strength with a mesh"), Part.IdleColorStrengthWithMesh, 0.20f);
	TestEqual(TEXT("idle strength on a bare hand"), Part.IdleColorStrengthBareHand, 0.35f);
	TestEqual(TEXT("idle secondary color scale"), Part.IdleSecondaryColorScale, 0.45f);
	TestEqual(TEXT("preview duration"), Part.PreviewDuration, 1.5f);
	return true;
}

#endif

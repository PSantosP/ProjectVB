// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Data/VBWeaponVisualConfig.h"
#include "Engine/StaticMesh.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
// 꺼내기 이펙트의 모양: 무기 메시가 있으면 그것, 메시 없는 주먹은 손, 아래팔 뼈만, 이펙트가 파라미터를 안 받으면 저장 검사가 막는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponShapeTest, "VB.Unit.Weapon.SummonShape",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBWeaponShapeTest::RunTest(const FString& Parameters)
{
	FVBWeaponVisualPart Part;
	TestTrue(TEXT("no parameter: no shape"), VBResolveWeaponShapeSource(Part) == EVBWeaponShapeSource::None);
	Part.ShapeMeshParameter = TEXT("ShapeMesh");
	TestTrue(TEXT("meshless without bones: no shape (never the whole body)"),
		VBResolveWeaponShapeSource(Part) == EVBWeaponShapeSource::None);
	Part.ShapeBones = {TEXT("hand_r"), TEXT("lowerarm_r")};
	TestTrue(TEXT("meshless with bones: body bones"), VBResolveWeaponShapeSource(Part) == EVBWeaponShapeSource::BodyBones);
	const TStrongObjectPtr<UStaticMesh> Sword(NewObject<UStaticMesh>());
	Part.Mesh = Sword.Get();
	TestTrue(TEXT("weapon mesh wins"), VBResolveWeaponShapeSource(Part) == EVBWeaponShapeSource::WeaponMesh);

	const TStrongObjectPtr<UVBWeaponVisualConfig> Config(NewObject<UVBWeaponVisualConfig>());
	const TStrongObjectPtr<UNiagaraSystem> Effect(NewObject<UNiagaraSystem>());
	Part.FormationEffect = Effect.Get();
	FVBWeaponVisualStyle Style;
	Style.Parts.Add(Part);
	Config->Styles.Add(EVBWeaponType::Blade, Style);
	TestEqual(TEXT("effect without the shape parameter is one problem"), Config->FindProblems().Num(), 1);
	return true;
}
#endif

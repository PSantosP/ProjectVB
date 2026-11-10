// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "VBWeaponDefinitionTestUtil.h"
#include "AbilitySystem/GameplayCues/VBGC_MeleeImpact.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBWeaponHitVisuals.h"
#include "GameplayEffectTypes.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace VBWeaponHitVisualsTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayEffectContextHandle ContextFrom(const UObject* Source)
	{
		FGameplayEffectContextHandle Context(new FGameplayEffectContext());
		Context.AddSourceObject(Source);
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitVisualsResolveTest, "VB.Unit.Combat.MeleeImpact.WeaponHitVisuals", VBWeaponHitVisualsTest::Flags)
bool FVBHitVisualsResolveTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponHitVisualsTest;
	const TStrongObjectPtr<UVBWeaponDefinition> Weapon(VBWeaponDefinitionTest::Make(GetTransientPackage(), EVBWeaponType::Blade));
	UVBWeaponHitVisuals* Visuals = NewObject<UVBWeaponHitVisuals>(Weapon.Get());

	TestNull(TEXT("no source object = the cue's own look"),
		UVBGC_MeleeImpact::ResolveHitVisuals(FGameplayEffectContextHandle(new FGameplayEffectContext())));
	TestNull(TEXT("a source that is not a weapon = the cue's own look"), UVBGC_MeleeImpact::ResolveHitVisuals(ContextFrom(Visuals)));
	TestNull(TEXT("weapon without visuals = the cue's own look"), UVBGC_MeleeImpact::ResolveHitVisuals(ContextFrom(Weapon.Get())));
	VBWeaponDefinitionTest::SetField(*Weapon, TEXT("HitVisuals"), TObjectPtr<const UVBWeaponHitVisuals>(Visuals));
	TestTrue(TEXT("weapon with visuals = its row"), UVBGC_MeleeImpact::ResolveHitVisuals(ContextFrom(Weapon.Get())) == Visuals);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitVisualsLayersTest, "VB.Unit.Combat.MeleeImpact.WeaponLayers", VBWeaponHitVisualsTest::Flags)
bool FVBHitVisualsLayersTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBWeaponHitVisuals> Visuals(NewObject<UVBWeaponHitVisuals>());
	Visuals->RippleSystem = NewObject<UNiagaraSystem>(Visuals.Get());
	Visuals->BloodSystem = NewObject<UNiagaraSystem>(Visuals.Get());
	Visuals->CutLineSystem = NewObject<UNiagaraSystem>(Visuals.Get());

	const UVBGC_MeleeImpact::FLayers None = UVBGC_MeleeImpact::PlanLayers(nullptr, true, true, FGameplayTag(), EVBHitImpact::Light);
	TestTrue(TEXT("no visuals plans no weapon layer (the cue's own look plays)"), !None.Ripple && !None.Blood && !None.CutLine
		&& None.Strikes.IsEmpty());

	const UVBGC_MeleeImpact::FLayers All = UVBGC_MeleeImpact::PlanLayers(Visuals.Get(), true, true, FGameplayTag(), EVBHitImpact::Light);
	TestTrue(TEXT("blood on, on body: all three"), All.Ripple && All.Blood && All.CutLine);

	const UVBGC_MeleeImpact::FLayers BloodOff = UVBGC_MeleeImpact::PlanLayers(Visuals.Get(), false, true, FGameplayTag(),
		EVBHitImpact::Light);
	TestTrue(TEXT("blood off drops only the blood burst"), BloodOff.Ripple && BloodOff.CutLine && !BloodOff.Blood);

	const UVBGC_MeleeImpact::FLayers OffBody = UVBGC_MeleeImpact::PlanLayers(Visuals.Get(), true, false, FGameplayTag(),
		EVBHitImpact::Light);
	TestTrue(TEXT("no body surface drops only the cut line"), OffBody.Ripple && OffBody.Blood && !OffBody.CutLine);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitVisualsValidationTest, "VB.Unit.Combat.MeleeImpact.WeaponHitVisualsValidation", VBWeaponHitVisualsTest::Flags)
bool FVBHitVisualsValidationTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBWeaponHitVisuals> Visuals(NewObject<UVBWeaponHitVisuals>());
	TestEqual(TEXT("no system at all is one problem"), Visuals->FindProblems().Num(), 1);
	Visuals->BloodSystem = NewObject<UNiagaraSystem>(Visuals.Get());
	TestEqual(TEXT("blood alone needs no named inputs"), Visuals->FindProblems().Num(), 0);
	Visuals->RippleSystem = NewObject<UNiagaraSystem>(Visuals.Get());
	TestEqual(TEXT("a ripple without its three inputs is three problems"), Visuals->FindProblems().Num(), 3);

	const TStrongObjectPtr<UVBWeaponHitVisuals> Strikes(NewObject<UVBWeaponHitVisuals>());
	Strikes->StrikeRows.AddDefaulted();
	TestEqual(TEXT("a strike row alone is not empty, but has no shape and no system"), Strikes->FindProblems().Num(), 2);
	Strikes->StrikeRows[0].Shapes.AddTag(VBGameplayTags::Melee_Shape);
	TestEqual(TEXT("the Melee.Shape root is not a part"), Strikes->FindProblems().Num(), 2);
	Strikes->StrikeRows[0].Shapes.Reset();
	Strikes->StrikeRows[0].Shapes.AddTag(VBGameplayTags::Melee_Shape_HandR);
	Strikes->StrikeRows[0].System = NewObject<UNiagaraSystem>(Strikes.Get());
	TestEqual(TEXT("a strike system without its two inputs is two problems"), Strikes->FindProblems().Num(), 2);
	Strikes->StrikeRows[0].Impacts = {EVBHitImpact::None};
	TestEqual(TEXT("tier None is never declared - one more problem"), Strikes->FindProblems().Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitVisualsStrikeRowsTest, "VB.Unit.Combat.MeleeImpact.StrikeRows", VBWeaponHitVisualsTest::Flags)
bool FVBHitVisualsStrikeRowsTest::RunTest(const FString& Parameters)
{
	// Fighter: 두 손엔 punch burst, slam 엔 hammer impact, 발차기엔 둘 다 없다.
	const TStrongObjectPtr<UVBWeaponHitVisuals> Visuals(NewObject<UVBWeaponHitVisuals>());
	FVBStrikeVisualRow& Punch = Visuals->StrikeRows.AddDefaulted_GetRef();
	Punch.Shapes.AddTag(VBGameplayTags::Melee_Shape_HandL);
	Punch.Shapes.AddTag(VBGameplayTags::Melee_Shape_HandR);
	Punch.System = NewObject<UNiagaraSystem>(Visuals.Get());
	FVBStrikeVisualRow& Slam = Visuals->StrikeRows.AddDefaulted_GetRef();
	Slam.Shapes.AddTag(VBGameplayTags::Melee_Shape_Slam);
	Slam.System = NewObject<UNiagaraSystem>(Visuals.Get());
	const FVBStrikeVisualRow* const PunchRow = &Visuals->StrikeRows[0];
	const FVBStrikeVisualRow* const SlamRow = &Visuals->StrikeRows[1];

	auto Plan = [&Visuals](const FGameplayTag& Shape, EVBHitImpact Impact = EVBHitImpact::Light)
	{
		return UVBGC_MeleeImpact::PlanLayers(Visuals.Get(), true, true, Shape, Impact).Strikes;
	};
	const auto Hand = Plan(VBGameplayTags::Melee_Shape_HandL);
	TestTrue(TEXT("a punch plays the punch burst only"), Hand.Num() == 1 && Hand[0] == PunchRow);
	const auto Hammer = Plan(VBGameplayTags::Melee_Shape_Slam);
	TestTrue(TEXT("a slam plays the hammer impact only"), Hammer.Num() == 1 && Hammer[0] == SlamRow);
	TestEqual(TEXT("a kick plays no strike row"), Plan(VBGameplayTags::Melee_Shape_FootR).Num(), 0);
	TestEqual(TEXT("an unknown part plays no strike row"), Plan(FGameplayTag()).Num(), 0);

	// 마무리 발차기 줄: 발 + 쓰러뜨림, 띄움 등급만. 보통 발차기는 여전히 아무것도 없다.
	FVBStrikeVisualRow& KickFinisher = Visuals->StrikeRows.AddDefaulted_GetRef();
	KickFinisher.Shapes.AddTag(VBGameplayTags::Melee_Shape_FootL);
	KickFinisher.Shapes.AddTag(VBGameplayTags::Melee_Shape_FootR);
	KickFinisher.Impacts = {EVBHitImpact::Knockdown, EVBHitImpact::Launch};
	KickFinisher.System = NewObject<UNiagaraSystem>(Visuals.Get());
	TestEqual(TEXT("an ordinary kick still plays no strike row"), Plan(VBGameplayTags::Melee_Shape_FootR, EVBHitImpact::Light).Num(), 0);
	// 부위 줄만 가진 무기(Fighter)의 보통 발차기는 무기 층이 비므로 큐 자신의 타격 모습을 잃지 않는다.
	const UVBGC_MeleeImpact::FLayers KickLayers = UVBGC_MeleeImpact::PlanLayers(Visuals.Get(), true, true,
		VBGameplayTags::Melee_Shape_FootR, EVBHitImpact::Light);
	TestFalse(TEXT("ordinary kick keeps the cue's own impact"), UVBGC_MeleeImpact::UsesWeaponLayers(Visuals.Get(), KickLayers));
	const UVBGC_MeleeImpact::FLayers PunchLayers = UVBGC_MeleeImpact::PlanLayers(Visuals.Get(), true, true,
		VBGameplayTags::Melee_Shape_HandR, EVBHitImpact::Light);
	TestTrue(TEXT("a punch uses the weapon layers"), UVBGC_MeleeImpact::UsesWeaponLayers(Visuals.Get(), PunchLayers));
	TestFalse(TEXT("no weapon visuals: the cue's own impact"), UVBGC_MeleeImpact::UsesWeaponLayers(nullptr, PunchLayers));
	const auto KickFinish = Plan(VBGameplayTags::Melee_Shape_FootL, EVBHitImpact::Knockdown);
	TestTrue(TEXT("a finisher kick plays the kick finisher"), KickFinish.Num() == 1 && KickFinish[0] == &Visuals->StrikeRows[2]);
	TestEqual(TEXT("the any-tier punch row still plays on a finisher punch"),
		Plan(VBGameplayTags::Melee_Shape_HandR, EVBHitImpact::Knockdown).Num(), 1);

	Visuals->StrikeRows[0].System = nullptr;
	TestEqual(TEXT("a row without a system is skipped, not spawned"), Plan(VBGameplayTags::Melee_Shape_HandR).Num(), 0);
	return true;
}
#endif

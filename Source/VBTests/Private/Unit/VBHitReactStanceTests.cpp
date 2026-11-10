#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Data/VBHitReactConfig.h"

namespace VBHitReactStanceTest
{
	FVBKnockdownSet MakeKnockdown()
	{
		FVBKnockdownSet Set;
		Set.FallMontage = NewObject<UAnimMontage>();
		Set.DownIdleMontage = NewObject<UAnimMontage>();
		Set.GetUpMontage = NewObject<UAnimMontage>();
		return Set;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitReactStanceResolveTest, "VB.Unit.HitReact.StanceOverrides.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FVBHitReactStanceResolveTest::RunTest(const FString& Parameters)
{
	UVBHitReactConfig* Config = NewObject<UVBHitReactConfig>();
	UAnimMontage* BaseHeavy = NewObject<UAnimMontage>();
	UAnimMontage* FighterHeavy = NewObject<UAnimMontage>();
	UAnimMontage* BladeBack = NewObject<UAnimMontage>();
	Config->HeavySet.Montage = BaseHeavy;
	Config->HeavyStanceOverrides.FindOrAdd(EVBWeaponType::Fighter).Montage = FighterHeavy;
	Config->HeavyStanceOverrides.FindOrAdd(EVBWeaponType::Blade).MontageBack = BladeBack;
	TestTrue(TEXT("Fighter heavy uses the victim stance"),
		Config->ResolveHeavyMontage(EVBWeaponType::Fighter, EVBHitDirection::Front) == FighterHeavy);
	TestTrue(TEXT("stance direction overrides the base"),
		Config->ResolveHeavyMontage(EVBWeaponType::Blade, EVBHitDirection::Back) == BladeBack);
	TestTrue(TEXT("missing stance direction falls back to base heavy"),
		Config->ResolveHeavyMontage(EVBWeaponType::Blade, EVBHitDirection::Front) == BaseHeavy);
	TestTrue(TEXT("enemy or missing stance retains base heavy"),
		Config->ResolveHeavyMontage(EVBWeaponType::None, EVBHitDirection::Front) == BaseHeavy);
	Config->HeavyStanceOverrides.Add(EVBWeaponType::BigSword, FVBHitReactSet{});
	TestTrue(TEXT("empty stance entry retains base heavy"),
		Config->ResolveHeavyMontage(EVBWeaponType::BigSword, EVBHitDirection::Front) == BaseHeavy);
	Config->DefaultSet.Montage = NewObject<UAnimMontage>();
	Config->HeavySet.Montage = nullptr;
	TestNull(TEXT("config heavy resolution does not silently choose a light clip"),
		Config->ResolveHeavyMontage(EVBWeaponType::None, EVBHitDirection::Front));

	Config->KnockdownSet = VBHitReactStanceTest::MakeKnockdown();
	FVBKnockdownSet FighterSet = VBHitReactStanceTest::MakeKnockdown();
	FighterSet.LaunchMontage = NewObject<UAnimMontage>();
	FighterSet.AirLoopMontage = NewObject<UAnimMontage>();
	FighterSet.LandMontage = NewObject<UAnimMontage>();
	FighterSet.AirSlamMontage = NewObject<UAnimMontage>();
	FighterSet.AirHitSet.Montage = NewObject<UAnimMontage>();
	Config->KnockdownStanceOverrides.Add(EVBWeaponType::Fighter, FighterSet);
	Config->KnockdownStanceOverrides.Add(EVBWeaponType::Blade, FVBKnockdownSet{});
	const FVBKnockdownSet* Chosen = Config->FindKnockdownSet(EVBWeaponType::Fighter);
	TestTrue(TEXT("Fighter selects its complete knockdown owner"), Chosen == Config->KnockdownStanceOverrides.Find(EVBWeaponType::Fighter));
	TestTrue(TEXT("launch, air hit and terminal reaction use that same set"), Chosen->LaunchMontage == FighterSet.LaunchMontage
		&& Chosen->AirHitSet.Montage == FighterSet.AirHitSet.Montage && Chosen->AirSlamMontage == FighterSet.AirSlamMontage);
	TestTrue(TEXT("missing knockdown stance uses base"), Config->FindKnockdownSet(EVBWeaponType::None) == &Config->KnockdownSet);
	TestTrue(TEXT("empty knockdown stance uses base"), Config->FindKnockdownSet(EVBWeaponType::Blade) == &Config->KnockdownSet);
	TestEqual(TEXT("a body without weapon state has no stance"), VBResolveVictimStance(nullptr), EVBWeaponType::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitReactStanceValidationTest, "VB.Unit.HitReact.StanceOverrides.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FVBHitReactStanceValidationTest::RunTest(const FString& Parameters)
{
	UVBHitReactConfig* Config = NewObject<UVBHitReactConfig>();
	Config->KnockdownSet = VBHitReactStanceTest::MakeKnockdown();
	Config->KnockdownStanceOverrides.Add(EVBWeaponType::Fighter, VBHitReactStanceTest::MakeKnockdown());
	TestTrue(TEXT("complete base and stance sets validate"), Config->FindProblems().IsEmpty());
	FVBKnockdownSet& Set = Config->KnockdownStanceOverrides.FindChecked(EVBWeaponType::Fighter);
	UAnimMontage* GetUp = Set.GetUpMontage;
	Set.GetUpMontage = nullptr;
	TestFalse(TEXT("partial stance knockdown reports a validation error"), Config->FindProblems().IsEmpty());
	TestTrue(TEXT("partial stance safely resolves to base while remaining invalid"),
		Config->FindKnockdownSet(EVBWeaponType::Fighter) == &Config->KnockdownSet);
	Set.GetUpMontage = GetUp;
	TestTrue(TEXT("restoring the same stance restores validity"), Config->FindProblems().IsEmpty());
	TestTrue(TEXT("restoring the same stance restores its selection"), Config->FindKnockdownSet(EVBWeaponType::Fighter) == &Set);

	FVBHitReactSet& Heavy = Config->HeavyStanceOverrides.FindOrAdd(EVBWeaponType::Fighter);
	Heavy.FrontAlternates.Add(NewObject<UAnimMontage>());
	TestFalse(TEXT("alternatives without a stance primary are invalid"), Config->FindProblems().IsEmpty());
	Heavy.MontageFront = NewObject<UAnimMontage>();
	TestTrue(TEXT("adding the required primary restores the same stance"), Config->FindProblems().IsEmpty());
	Config->HeavySet.Alternates.Add(NewObject<UAnimMontage>());
	TestFalse(TEXT("base heavy obeys the same validation rule"), Config->FindProblems().IsEmpty());
	Config->HeavySet.Montage = NewObject<UAnimMontage>();
	TestTrue(TEXT("base and override validation agree after restoration"), Config->FindProblems().IsEmpty());
	return true;
}
#endif

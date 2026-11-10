// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Data/VBHitPhysicsConfig.h"
#include "Data/VBHitReactConfig.h"
#include "Data/VBSwingCombatProfile.h"
#include "PhysicsEngine/PhysicsAsset.h"

// 적 피격 손맛의 순수 규칙: 대체 클립 고르기, 물리 층 블렌드 곡선, 스윙 띄움 높이.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitReactPickIndexTest, "VB.Unit.EnemyHitFeel.PickIndex",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitReactPickIndexTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("One option is always 0"), VBPickHitReactIndex(1, 7, 0), 0);
	TestEqual(TEXT("Zero options returns 0"), VBPickHitReactIndex(0, 7, INDEX_NONE), 0);
	for (int32 Num = 2; Num <= 5; ++Num)
	{
		int32 Last = INDEX_NONE;
		TSet<int32> Seen;
		for (int32 Sequence = 1; Sequence <= 200; ++Sequence)
		{
			const int32 Index = VBPickHitReactIndex(Num, Sequence, Last);
			if (!TestTrue(FString::Printf(TEXT("N=%d seq=%d in range"), Num, Sequence), Index >= 0 && Index < Num) ||
				!TestNotEqual(FString::Printf(TEXT("N=%d seq=%d never repeats the previous pick"), Num, Sequence), Index, Last))
			{
				return false;
			}

			TestEqual(TEXT("Same input gives the same pick (server and owner client agree)"),
				VBPickHitReactIndex(Num, Sequence, Last), Index);
			Seen.Add(Index);
			Last = Index;
		}

		TestEqual(FString::Printf(TEXT("N=%d every option shows up"), Num), Seen.Num(), Num);
	}

	TestTrue(TEXT("Out-of-range last index is treated as none"), VBPickHitReactIndex(3, 5, 9) < 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitReactSetAlternatesTest, "VB.Unit.EnemyHitFeel.SetAlternates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitReactSetAlternatesTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Any = NewObject<UAnimMontage>();
	UAnimMontage* AnyAlt = NewObject<UAnimMontage>();
	UAnimMontage* Front = NewObject<UAnimMontage>();
	UAnimMontage* FrontAlt = NewObject<UAnimMontage>();
	FVBHitReactSet Set;
	Set.Montage = Any;
	Set.Alternates = {AnyAlt, nullptr};
	Set.MontageFront = Front;
	Set.FrontAlternates = {FrontAlt};

	TestEqual(TEXT("No pick returns the primary"), Set.ResolveByDirection(EVBHitDirection::Front), Front);
	FVBHitReactPick Pick;
	TSet<UAnimMontage*> FrontSeen;
	TSet<UAnimMontage*> BackSeen;
	for (int32 i = 0; i < 20; ++i)
	{
		++Pick.Sequence;
		FrontSeen.Add(Set.ResolveByDirection(EVBHitDirection::Front, &Pick));
		++Pick.Sequence;
		BackSeen.Add(Set.ResolveByDirection(EVBHitDirection::Back, &Pick));
	}
	// 앞, 뒤를 번갈아 맞아도 각 방향이 자기 후보를 다 쓴다 (방향별로 따로 기억하지 않으면 한 방향이 한 클립에 갇힌다)
	TestTrue(TEXT("Front picks only from front primary + front alternates"), FrontSeen.Num() == 2 && FrontSeen.Contains(Front)
		&& FrontSeen.Contains(FrontAlt));
	TestTrue(TEXT("Empty back slot falls to non-directional + its alternates, skipping null"), BackSeen.Num() == 2
		&& BackSeen.Contains(Any) && BackSeen.Contains(AnyAlt));

	FVBHitReactPick SameSide;
	UAnimMontage* Previous = nullptr;
	for (int32 i = 0; i < 20; ++i)
	{
		++SameSide.Sequence;
		UAnimMontage* Now = Set.ResolveByDirection(EVBHitDirection::Front, &SameSide);
		TestNotEqual(TEXT("Same side never plays the same clip twice in a row"), Now, Previous);
		Previous = Now;
	}

	// 같은 클립을 대체 칸에 또 넣어도(주 클립 포함) 연달아 나오지 않는다 - 후보는 클립 단위로 센다.
	UAnimMontage* Left = NewObject<UAnimMontage>();
	UAnimMontage* LeftAlt = NewObject<UAnimMontage>();
	Set.MontageLeft = Left;
	Set.LeftAlternates = {Left, LeftAlt, LeftAlt};
	FVBHitReactPick LeftPick;
	UAnimMontage* PreviousLeft = nullptr;
	for (int32 i = 0; i < 20; ++i)
	{
		++LeftPick.Sequence;
		UAnimMontage* Now = Set.ResolveByDirection(EVBHitDirection::Left, &LeftPick);
		TestNotEqual(TEXT("Duplicated alternates never repeat a clip"), Now, PreviousLeft);
		PreviousLeft = Now;
	}

	TestTrue(TEXT("Contains sees alternates"), Set.Contains(FrontAlt) && Set.Contains(AnyAlt) && Set.Contains(Front));
	TestFalse(TEXT("Contains rejects others and null"), Set.Contains(NewObject<UAnimMontage>()) || Set.Contains(nullptr));

	FVBHitReactSet Empty;
	FVBHitReactPick EmptyPick;
	TestNull(TEXT("Empty set with pick stays null"), Empty.ResolveByDirection(EVBHitDirection::Left, &EmptyPick));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitPhysicsBlendTest, "VB.Unit.EnemyHitFeel.PhysicsBlend",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitPhysicsBlendTest::RunTest(const FString& Parameters)
{
	FVBHitPhysicsTier Tier;
	Tier.ImpulseSpeed = 300.0f;
	Tier.MaxBlendWeight = 0.4f;
	Tier.RiseSeconds = 0.1f;
	Tier.FallSeconds = 0.4f;
	TestEqual(TEXT("Starts at 0"), Tier.EvaluateBlend(0.0f), 0.0f);
	TestEqual(TEXT("Half way up"), Tier.EvaluateBlend(0.05f), 0.2f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Peak at end of rise"), Tier.EvaluateBlend(0.1f), 0.4f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Half way down"), Tier.EvaluateBlend(0.3f), 0.2f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Zero at the end"), Tier.EvaluateBlend(0.5f), 0.0f);
	TestEqual(TEXT("Zero before start"), Tier.EvaluateBlend(-0.1f), 0.0f);
	Tier.RiseSeconds = 0.0f;
	TestEqual(TEXT("Zero rise starts at peak"), Tier.EvaluateBlend(0.0f), 0.4f, KINDA_SMALL_NUMBER);

	UVBHitPhysicsConfig* Config = NewObject<UVBHitPhysicsConfig>();
	FVBHitPhysicsTier Off;
	Config->Tiers.Add(EVBHitImpact::Light, Tier);
	Config->Tiers.Add(EVBHitImpact::Heavy, Off);
	TestNotNull(TEXT("Authored tier is found"), Config->FindActiveTier(EVBHitImpact::Light));
	TestNull(TEXT("Zero tier counts as off"), Config->FindActiveTier(EVBHitImpact::Heavy));
	TestNull(TEXT("Missing tier counts as off"), Config->FindActiveTier(EVBHitImpact::Launch));
	return true;
}

// 저작된 설정은 적 physics asset 에 BODY 가 있는 뼈를 가리켜야 한다 - 없으면 물리 층 전체가 꺼진 채다
// 대조: PA_Mannequin 에서 body 가 없는 spine_01 이 실제로 없다고 확인해 잘못된 값을 잡아냄을 보인다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitPhysicsRootBodyTest, "VB.Unit.EnemyHitFeel.PhysicsRootHasBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitPhysicsRootBodyTest::RunTest(const FString& Parameters)
{
	const UVBHitPhysicsConfig* Config = LoadObject<UVBHitPhysicsConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_HitPhysics_Default.DA_HitPhysics_Default"));
	const UPhysicsAsset* Physics = LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/Characters/Mannequins/Rigs/PA_Mannequin.PA_Mannequin"));
	if (!TestNotNull(TEXT("hit physics config"), Config) || !TestNotNull(TEXT("enemy physics asset"), Physics))
	{
		return false;
	}

	TestNotEqual(TEXT("SimulateRootBone has a body in the enemy physics asset"), Physics->FindBodyIndex(Config->SimulateRootBone),
		static_cast<int32>(INDEX_NONE));
	TestNotEqual(TEXT("FallbackHitBone has a body in the enemy physics asset"), Physics->FindBodyIndex(Config->FallbackHitBone),
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Control: spine_01 (the old value) has no body"), Physics->FindBodyIndex(TEXT("spine_01")),
		static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSwingLaunchHeightTest, "VB.Unit.EnemyHitFeel.SwingLaunchHeight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBSwingLaunchHeightTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No profile keeps the attack height"), UVBSwingCombatProfile::ResolveLaunchHeight(nullptr, 40.0f), 40.0f);
	UVBSwingCombatProfile* Profile = NewObject<UVBSwingCombatProfile>();
	TestEqual(TEXT("Default override keeps the attack height"), UVBSwingCombatProfile::ResolveLaunchHeight(Profile, 40.0f), 40.0f);
	Profile->LaunchHeightOverride = 250.0f;
	TestEqual(TEXT("Override wins"), UVBSwingCombatProfile::ResolveLaunchHeight(Profile, 40.0f), 250.0f);
	Profile->LaunchHeightOverride = 0.0f;
	TestEqual(TEXT("Zero is an explicit no-launch"), UVBSwingCombatProfile::ResolveLaunchHeight(Profile, 40.0f), 0.0f);
	return true;
}

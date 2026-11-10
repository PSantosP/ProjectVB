// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Character/VBGuardable.h"
#include "Data/VBParryConfig.h"
#include "VBInputDispatchFixtureAbilities.h" // UVBInputFixtureRiposte - same input and window requirement, no riposte asset tag
#include "VBLoadingScreenTestProbe.h"       // AVBLoadingScreenTestAbilityPawn - a pawn with a real ASC
#include "VBPlayerBodyTestFixture.h"

// 반격 창은 반격을 가진 몸에게만 연다.
//   음성 대조: 규칙이 늘 설정 길이를 돌려주면 "빈 ASC" 와 "태그 없는 능력" 행이, 몸이 길이 0 에서 바로 돌아가면 "길이 0 이어도 락온" 행이 실패해야 한다.
namespace VBRiposteWindowGateTest
{
	// 실제 반격 - 게임에서는 BP_VBPlayerState 의 StartupAbilitySet 이 이 클래스를 시작 능력으로 준다.
	UClass* LoadShippedRiposte()
	{
		return StaticLoadClass(UGameplayAbility::StaticClass(), nullptr,
			TEXT("/Game/VB/GAS/Abilities/BP_GA_Riposte.BP_GA_Riposte_C"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRiposteWindowRuleTest, "VB.Unit.Combat.RiposteWindowGate.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRiposteWindowRuleTest::RunTest(const FString& Parameters)
{
	const UVBParryConfig& Config = *GetDefault<UVBParryConfig>();
	if (!TestTrue(TEXT("the parry config opens a window at all"), Config.RiposteWindow > 0.0f))
	{
		return false;
	}

	UClass* Riposte = VBRiposteWindowGateTest::LoadShippedRiposte();
	if (!TestNotNull(TEXT("shipped BP_GA_Riposte"), Riposte))
	{
		return false;
	}

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	TStrongObjectPtr<UPackage> Package(World->GetPackage());
	World->InitializeActorsForPlay(FURL());
	if (AVBLoadingScreenTestAbilityPawn* Pawn = World->SpawnActor<AVBLoadingScreenTestAbilityPawn>(); TestNotNull(TEXT("pawn"), Pawn))
	{
		UAbilitySystemComponent& ASC = *Pawn->AbilitySystem;
		ASC.InitAbilityActorInfo(Pawn, Pawn);
		TestEqual(TEXT("no abilities: no window"), VBGuardRules::RiposteWindowSeconds(ASC, Config), 0.0f);
		ASC.GiveAbility(FGameplayAbilitySpec(UVBInputFixtureRiposte::StaticClass()));
		TestEqual(TEXT("riposte-shaped ability without the riposte tag: no window"), VBGuardRules::RiposteWindowSeconds(ASC, Config), 0.0f);
		ASC.GiveAbility(FGameplayAbilitySpec(Riposte));
		TestEqual(TEXT("shipped riposte owned: the configured window"), VBGuardRules::RiposteWindowSeconds(ASC, Config),
			Config.RiposteWindow);
	}

	World->DestroyWorld(false);
	Package->SetDirtyFlag(false);
	return true;
}

// 실제 몸(AVBCharacter)의 OpenRiposteWindow: 길이 0 은 창 없이 락온만(열린 창은 닫는다), 길이가 있으면 창 한 몫, 몸을 놓으면 닫힌다(C1 반납).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRiposteWindowBodyTest, "VB.Unit.Combat.RiposteWindowGate.Body",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRiposteWindowBodyTest::RunTest(const FString& Parameters)
{
	const UVBParryConfig& Config = *GetDefault<UVBParryConfig>();
	if (!TestTrue(TEXT("the parry config locks onto the parried attacker"), Config.bAutoLockOnParry))
	{
		return false;
	}

	VBPlayerBodyTest::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.A);
	IVBGuardable& Guard = *F.A;

	Guard.OpenRiposteWindow(0.0f, F.Target);
	TestEqual(TEXT("length 0: no window tag"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_RiposteWindow), 0);
	TestTrue(TEXT("length 0: still locked on the parried attacker"), UVBTargetLockComponent::FindFor(F.A)->GetLockedTarget() == F.Target);

	Guard.OpenRiposteWindow(Config.RiposteWindow, F.Target);
	TestEqual(TEXT("configured length: one window share"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_RiposteWindow), 1);
	Guard.OpenRiposteWindow(0.0f, F.Target);
	TestEqual(TEXT("length 0 while a window is open: the old window is closed"),
		F.ASC->GetTagCount(VBGameplayTags::State_Combat_RiposteWindow), 0);

	Guard.OpenRiposteWindow(Config.RiposteWindow, F.Target);
	F.PC->Possess(F.B);
	TestEqual(TEXT("body released: the window is closed"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_RiposteWindow), 0);
	return true;
}
#endif

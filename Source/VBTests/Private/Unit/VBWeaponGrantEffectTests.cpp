// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Character/VBWeaponRules.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "VBWeaponDefinitionTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VBWeaponGrantTest
{
	// 사실 저장소 + 무기 상태를 가진 빙의 캐릭터(Fighter 정의만). 문맥은 그 플레이어로 채운다.
	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		UVBStoryStateComponent* Story = nullptr;
		UVBWeaponStateComponent* Weapons = nullptr;
		FVBRuleContext Context;

		~FFixture() { if (World) { World->DestroyWorld(false); Package->SetDirtyFlag(false); } }

		bool Init(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->InitializeActorsForPlay(FURL());
			AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
			APlayerController* PC = World->SpawnActor<APlayerController>();
			APlayerState* State = World->SpawnActor<APlayerState>();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACharacter* Body = World->SpawnActor<ACharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Test.TestTrue(TEXT("actors"), GameState && PC && State && Body))
			{
				return false;
			}

			World->SetGameState(GameState);
			Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();
			State->SetOwner(PC);
			PC->PlayerState = State;
			PC->Possess(Body);
			Weapons = NewObject<UVBWeaponStateComponent>(Body);
			Body->AddInstanceComponent(Weapons);
			Weapons->RegisterComponent();
			VBWeaponDefinitionTest::Equip(*Weapons, { VBWeaponDefinitionTest::Make(Weapons, EVBWeaponType::Fighter) }, EVBWeaponType::None);
			Context.World = World;
			Context.Story = Story;
			Context.Instigator = State;
			return true;
		}
	};

	FInstancedStruct SetFact(const FGameplayTag& Fact)
	{
		FVBEffect_SetFact Effect;
		Effect.Fact = Fact;
		Effect.bOn = true;
		return FInstancedStruct::Make(Effect);
	}
}

// 무기 주기 효과는 무기를 가진 뒤에만(새로, 이미) OnGrantedEffects 를 잇고, 주지 못하면 잇지 않는다.
//   대조: 결과를 보지 않고 늘 이으면 "failed grant" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponGrantEffectTest, "VB.Unit.Weapon.GrantEffectContinuation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponGrantEffectTest::RunTest(const FString& Parameters)
{
	VBWeaponGrantTest::FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* Story = Fixture.Story;
	UVBWeaponStateComponent* Weapons = Fixture.Weapons;
	const FVBRuleContext& Context = Fixture.Context;

	const FGameplayTag Gained = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.ErrandDone"));
	FVBEffect_SetFact MarkGained;
	MarkGained.Fact = Gained;
	MarkGained.bOn = true;
	FVBEffect_GrantWeapon Grant;
	Grant.OnGrantedEffects.Add(FInstancedStruct::Make(MarkGained));

	AddExpectedMessagePlain(TEXT("효과(무기 주기)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("무기 주기 실패"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Grant.Weapon = EVBWeaponType::Blade; // 정의가 없는 무기 - Failed
	Grant.Apply(Context);
	TestFalse(TEXT("failed grant: the continuation does not run"), Story->HasFact(Gained));

	Grant.Weapon = EVBWeaponType::Fighter;
	Grant.Apply(Context);
	TestTrue(TEXT("newly owned: the weapon is owned"), Weapons->IsWeaponOwned(EVBWeaponType::Fighter));
	TestTrue(TEXT("newly owned: the continuation runs"), Story->HasFact(Gained));

	Story->SetFact(Gained, false);
	Grant.Apply(Context);
	TestTrue(TEXT("already owned: the continuation runs again (recovers a cut-off earlier attempt)"), Story->HasFact(Gained));
	return true;
}

// 결과를 따지는 적용(장면 완료 효과의 길)은 무기 주기 실패에서 멈추고 Failed 를 낸다 - 이어지는 사실도, 뒤의 효과도 없다.
//   대조: 무기 주기가 실패를 알리지 않거나 적용 길이 알림을 보지 않으면 "failed grant" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponGrantFailureReportTest, "VB.Unit.Weapon.GrantFailureReported",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponGrantFailureReportTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponGrantTest;
	FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	const FGameplayTag Before = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.Talked"));
	const FGameplayTag Gained = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.ErrandDone"));
	const FGameplayTag After = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.Never"));
	auto Run = [&Fixture](const TArray<FInstancedStruct>& Effects, int32 StopAt = INDEX_NONE)
	{
		int32 Prepared = 0;
		return VBRules::ApplyAllUntilFailure(Effects, [&](FVBRuleContext& Context)
		{
			if (Prepared++ == StopAt)
			{
				return false;
			}

			Context = Fixture.Context;
			return true;
		});
	};
	auto Grant = [](EVBWeaponType Weapon, TArray<FInstancedStruct> OnGranted)
	{
		FVBEffect_GrantWeapon Effect;
		Effect.Weapon = Weapon;
		Effect.OnGrantedEffects = MoveTemp(OnGranted);
		return FInstancedStruct::Make(Effect);
	};
	auto Reset = [&]() { for (const FGameplayTag& Fact : {Before, Gained, After}) Fixture.Story->SetFact(Fact, false); };
	AddExpectedMessagePlain(TEXT("효과(무기 주기)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("무기 주기 실패"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("해내지 못"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

	TArray<FInstancedStruct> Effects = {SetFact(Before), Grant(EVBWeaponType::Blade, {SetFact(Gained)}), SetFact(After)};
	TestTrue(TEXT("failed grant: the run reports Failed"), Run(Effects) == VBRules::EApplyResult::Failed);
	TestTrue(TEXT("failed grant: earlier effects stay applied"), Fixture.Story->HasFact(Before));
	TestFalse(TEXT("failed grant: no continuation"), Fixture.Story->HasFact(Gained));
	TestFalse(TEXT("failed grant: later effects do not run"), Fixture.Story->HasFact(After));

	Reset();
	Effects = {SetFact(Before), Grant(EVBWeaponType::Fighter, {SetFact(Gained)}), SetFact(After)};
	TestTrue(TEXT("granted: Landed"), Run(Effects) == VBRules::EApplyResult::Landed);
	TestTrue(TEXT("granted: every effect ran"), Fixture.Story->HasFact(Before) && Fixture.Story->HasFact(Gained)
		&& Fixture.Story->HasFact(After));

	Reset();
	Effects = {Grant(EVBWeaponType::Fighter, {Grant(EVBWeaponType::Blade, {}), SetFact(Gained)})};
	TestTrue(TEXT("nested failed grant reaches the outer run"), Run(Effects) == VBRules::EApplyResult::Failed);

	Reset();
	Effects = {SetFact(Before), SetFact(After)};
	TestTrue(TEXT("caller ends between effects: Interrupted"), Run(Effects, 1) == VBRules::EApplyResult::Interrupted);
	TestFalse(TEXT("interrupted: the next effect does not run"), Fixture.Story->HasFact(After));

	Reset();
	Effects = {FInstancedStruct::Make(FVBCondition_Fact())};
	TestTrue(TEXT("a non-effect element is Failed, not Landed"), Run(Effects) == VBRules::EApplyResult::Failed);
	return true;
}

#endif

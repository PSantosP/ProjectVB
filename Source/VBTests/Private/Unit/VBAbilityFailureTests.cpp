#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include <atomic>
#include "AbilitySystemGlobals.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Data/VBAbilityFailureMessageData.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerState.h"
#include "UI/Notice/VBNoticeProducerComponent.h"
#include "UI/Notice/VBNoticeSubsystem.h"
#include "VBAbilityFailureTestAbilities.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

struct FVBAbilityFailureTestAccess
{
	static UVBAbilityFailureMessageData* Messages(const UVBNoticeProducerComponent& Producer) { return Producer.AbilityFailureMessages; }
	static void SetMessages(UVBNoticeProducerComponent& Producer, UVBAbilityFailureMessageData* Data) { Producer.AbilityFailureMessages = Data; }
	static void Begin(UVBNoticeProducerComponent& Producer, UVBAbilityFailureMessageData* Data)
	{
		Producer.AbilityFailureMessages = Data;
		Producer.BeginPlay();
	}

	static void End(UVBNoticeProducerComponent& Producer) { Producer.EndPlay(EEndPlayReason::RemovedFromWorld); }
};

namespace VBFailureTests
{
	struct FFailureLog : FOutputDevice
	{
		FFailureLog()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
			// AddOutputDevice 가 쌓인 로그를 다시 보낼 수 있어 이 범위의 이후 시도만 센다.
			Count.store(0, std::memory_order_relaxed);
		}

		~FFailureLog() { if (GLog) GLog->RemoveOutputDevice(this); }
		// 기본 device 는 버퍼를 쓴다 - thread-safe device 는 줄마다 내보낸 스레드에서 바로 받는다.
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		int32 GetCount() const { return Count.load(std::memory_order_relaxed); }
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogVB")) && FString(Message).StartsWith(TEXT("[AbilityFailure]")))
			{
				Count.fetch_add(1, std::memory_order_relaxed);
			}
		}
	private:
		std::atomic<int32> Count{0};
	};

	void AddRow(UVBAbilityFailureMessageData& Data, FGameplayTag Tag, const TCHAR* Text, int32 Priority = 0)
	{
		FVBAbilityFailureMessageRow& Row = Data.Rows.AddDefaulted_GetRef();
		Row.FailureTag = Tag;
		Row.Message = FText::FromString(Text);
		Row.Priority = Priority;
	}

	void AddSilentRow(UVBAbilityFailureMessageData& Data, FGameplayTag Tag)
	{
		FVBAbilityFailureMessageRow& Row = Data.Rows.AddDefaulted_GetRef();
		Row.FailureTag = Tag;
		Row.bSilent = true;
	}

	// 등록된 Ability.Fail 이유마다 한 줄 - 저장 시 검사가 요구하는 모양.
	UVBAbilityFailureMessageData* MakeCompleteData()
	{
		UVBAbilityFailureMessageData* Data = NewObject<UVBAbilityFailureMessageData>();
		for (const FGameplayTag& Reason : UVBAbilityFailureMessageData::RequiredReasons())
		{
			AddRow(*Data, Reason, *Reason.ToString());
		}

		return Data;
	}

	bool SettingsReady(FAutomationTestBase& Test)
	{
		// 실제 시작 설정을 읽는다 - 설정이 틀린 프로젝트를 통과시키려고 Globals 나 CDO 를 고치지 않는다.
		UAbilitySystemGlobals::Get();
		const auto* Settings = GetDefault<UGameplayAbilitiesDeveloperSettings>();
		bool bReady = Test.TestEqual(TEXT("configured cooldown failure tag"), Settings->ActivateFailCooldownTag,
			FGameplayTag(VBGameplayTags::Ability_Fail_Cooldown));
		bReady &= Test.TestEqual(TEXT("configured cost failure tag"), Settings->ActivateFailCostTag,
			FGameplayTag(VBGameplayTags::Ability_Fail_Cost));
		bReady &= Test.TestEqual(TEXT("configured networking failure tag"), Settings->ActivateFailNetworkingTag,
			FGameplayTag(VBGameplayTags::Ability_Fail_Networking));
		bReady &= Test.TestEqual(TEXT("configured blocked failure tag"), Settings->ActivateFailTagsBlockedTag,
			FGameplayTag(VBGameplayTags::Ability_Fail_TagsBlocked));
		bReady &= Test.TestEqual(TEXT("configured missing failure tag"), Settings->ActivateFailTagsMissingTag,
			FGameplayTag(VBGameplayTags::Ability_Fail_TagsMissing));
		return bReady;
	}

	struct FFixture
	{
		UWorld* World = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
		AVBPlayerController* PC = nullptr;
		AVBPlayerState* State = nullptr;
		APawn* Pawn = nullptr;
		UVBAbilitySystemComponent* ASC = nullptr;
		UVBNoticeProducerComponent* Producer = nullptr;
		UVBNoticeSubsystem* Notices = nullptr;
		FActiveGameplayEffectHandle Cooldown;
		bool bProducerStarted = false;

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!SettingsReady(Test) || !Test.TestNotNull(TEXT("engine"), GEngine))
			{
				return false;
			}

			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			World->GetOutermost()->SetFlags(RF_Transient);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			PC = World->SpawnActor<AVBPlayerController>();
			State = World->SpawnActor<AVBPlayerState>();
			Pawn = World->SpawnActor<APawn>();
			if (!PC || !State || !Pawn) { Test.AddError(TEXT("fixture actors failed to spawn")); return false; }
			LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
			PC->Player = LocalPlayer;
			LocalPlayer->PlayerController = PC;
			LocalPlayer->PlayerAdded(nullptr, 0);
			State->SetOwner(PC);
			PC->PlayerState = State;
			PC->OnRep_PlayerState();
			PC->Possess(Pawn);
			ASC = Cast<UVBAbilitySystemComponent>(State->GetAbilitySystemComponent());
			Notices = UVBNoticeSubsystem::Get(PC);
			Producer = PC->FindComponentByClass<UVBNoticeProducerComponent>();
			if (!Test.TestNotNull(TEXT("ASC"), ASC) || !Test.TestNotNull(TEXT("notice sink"), Notices)
				|| !Test.TestNotNull(TEXT("notice producer"), Producer))
			{
				return false;
			}

			ASC->InitAbilityActorInfo(State, Pawn);
			UVBAbilityFailureMessageData* Data = NewObject<UVBAbilityFailureMessageData>(Producer);
			AddRow(*Data, VBGameplayTags::Ability_Fail_Cooldown, TEXT("Cooldown fixture"), 10);
			AddRow(*Data, VBGameplayTags::Ability_Fail_Unknown, TEXT("Unknown fixture"));
			AddSilentRow(*Data, VBGameplayTags::Ability_Fail_NoOwnedWeapon);
			FVBAbilityFailureTestAccess::Begin(*Producer, Data);
			bProducerStarted = true;
			Cooldown = ASC->ApplyGameplayEffectToSelf(GetDefault<UVBAbilityFailureCooldownEffect>(), 1.f, ASC->MakeEffectContext());
			return Test.TestTrue(TEXT("real cooldown effect applied"), Cooldown.IsValid())
				&& Test.TestTrue(TEXT("real effect grants cooldown tag"), ASC->HasMatchingGameplayTag(VBGameplayTags::Cooldown_Execute));
		}

		void StopProducer()
		{
			if (bProducerStarted)
			{
				FVBAbilityFailureTestAccess::End(*Producer);
			}

			bProducerStarted = false;
		}

		~FFixture()
		{
			StopProducer();
			if (ASC)
			{
				ASC->CancelAllAbilities();
			}

			if (LocalPlayer)
			{
				LocalPlayer->PlayerRemoved();
				if (PC)
				{
					PC->Player = nullptr;
				}

				LocalPlayer->PlayerController = nullptr;
			}

			if (World)
			{
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}

				World->DestroyWorld(false);
			}
		}
	};

	bool RunBatch(FAutomationTestBase& Test, bool bOptIn, bool bThenSuccess)
	{
		FFixture Fixture;
		if (!Fixture.Initialize(Test))
		{
			return false;
		}

		UVBAbilitySystemComponent& ASC = *Fixture.ASC;
		const TSubclassOf<UGameplayAbility> FailureClass = bOptIn ? UVBAbilityFailureOptInExecute::StaticClass() : UVBAbilityFailureOptOutExecute::StaticClass();
		const FGameplayAbilitySpecHandle Failed = ASC.GiveAbility(FGameplayAbilitySpec(FailureClass, 1));
		FGameplayAbilitySpecHandle Success;
		if (bThenSuccess)
		{
			Success = ASC.GiveAbility(FGameplayAbilitySpec(UVBAbilityFailureSuccess::StaticClass(), 1));
		}

		int32 EngineFailures = 0;
		FGameplayTagContainer ObservedTags;
		const FDelegateHandle Observer = ASC.AbilityFailedCallbacks.AddLambda([&](const UGameplayAbility*,
			const FGameplayTagContainer& Reasons)
		{
			++EngineFailures;
			ObservedTags.AppendTags(Reasons);
		});
		const int32 Before = Fixture.Notices->GetEntries().Num();
		int32 FeedChanges = 0;
		int32 Presentations = 0;
		const auto Changed = Fixture.Notices->OnNoticesChanged.AddLambda([&]() { ++FeedChanges; });
		const auto Presented = ASC.OnPlayerAbilityFailure.AddLambda([&](const FVBAbilityFailureReport&) { ++Presentations; });
		FFailureLog Log;
		ASC.AbilityInputTagPressed(VBGameplayTags::Input_Execute);
		Test.TestEqual(TEXT("one real engine Notify"), EngineFailures, 1);
		Test.TestEqual(TEXT("one report line for the real rejection"), Log.GetCount(), 1);
		Test.TestTrue(TEXT("engine supplies configured cooldown reason"), ObservedTags.HasTagExact(VBGameplayTags::Ability_Fail_Cooldown));
		Test.TestFalse(TEXT("Execute stays inactive"), ASC.FindAbilitySpecFromHandle(Failed)->IsActive());
		const int32 ExpectedNotices = bOptIn && !bThenSuccess ? 1 : 0;
		Test.TestEqual(TEXT("one presentation request only when eligible"), Presentations, ExpectedNotices);
		Test.TestEqual(TEXT("one actual feed change only when eligible"), FeedChanges, ExpectedNotices);
		Test.TestEqual(TEXT("actual notice feed delta"), Fixture.Notices->GetEntries().Num() - Before, ExpectedNotices);
		if (ExpectedNotices)
		{
			const FVBNoticeEntry& Entry = Fixture.Notices->GetEntries().Last();
			Test.TestEqual(TEXT("message selected from data"), Entry.Text.ToString(), FString(TEXT("Cooldown fixture")));
			Test.TestEqual(TEXT("one notice rather than a merged duplicate"), Entry.Count, 1);
		}

		if (bThenSuccess)
		{
			const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Success);
			Test.TestTrue(TEXT("later candidate really activated"), Spec && Spec->IsActive());
			const UVBAbilityFailureSuccess* Instance = Spec ? Cast<UVBAbilityFailureSuccess>(Spec->GetPrimaryInstance()) : nullptr;
			if (Test.TestNotNull(TEXT("successful ExecuteAbility instance"), Instance))
			{
				Test.TestEqual(TEXT("real ExecuteAbility ran once"), Instance->ExecuteCount, 1);
			}
		}

		ASC.AbilityFailedCallbacks.Remove(Observer);
		ASC.OnPlayerAbilityFailure.Remove(Presented);
		Fixture.Notices->OnNoticesChanged.Remove(Changed);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureOptInTest, "VB.Unit.AbilityFailure.OptInCooldown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureOptInTest::RunTest(const FString& Parameters) { return VBFailureTests::RunBatch(*this, true, false); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureOptOutTest, "VB.Unit.AbilityFailure.OptOutCooldown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureOptOutTest::RunTest(const FString& Parameters) { return VBFailureTests::RunBatch(*this, false, false); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureThenSuccessTest, "VB.Unit.AbilityFailure.FailThenSuccess",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureThenSuccessTest::RunTest(const FString& Parameters) { return VBFailureTests::RunBatch(*this, true, true); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureSettingsTest, "VB.Unit.AbilityFailure.EngineFailureTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureSettingsTest::RunTest(const FString& Parameters) { return VBFailureTests::SettingsReady(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureCandidatesTest, "VB.Unit.AbilityFailure.CandidateBoundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureCandidatesTest::RunTest(const FString& Parameters)
{
	using namespace VBFailureTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *Fixture.ASC;
	FFailureLog Log;
	const int32 Before = Fixture.Notices->GetEntries().Num();
	ASC.AbilityInputTagPressed(VBGameplayTags::Input_Execute);
	TestEqual(TEXT("no matching spec has no report"), Log.GetCount(), 0);
	TestEqual(TEXT("no matching spec has no notice"), Fixture.Notices->GetEntries().Num(), Before);
	ASC.GiveAbility(FGameplayAbilitySpec(UVBAbilityFailureOptOutExecute::StaticClass(), 1));
	const auto OptIn = ASC.GiveAbility(FGameplayAbilitySpec(UVBAbilityFailureOptInExecute::StaticClass(), 1));
	FVBAbilityFailureReport Presented;
	int32 Presentations = 0;
	const auto Observer = ASC.OnPlayerAbilityFailure.AddLambda([&](const FVBAbilityFailureReport& Report)
	{
		Presented = Report;
		++Presentations;
	});
	ASC.AbilityInputTagPressed(VBGameplayTags::Input_Execute);
	TestEqual(TEXT("two actual candidate rejections keep two reports"), Log.GetCount(), 2);
	TestEqual(TEXT("all failed candidates produce one presentation"), Presentations, 1);
	TestTrue(TEXT("first failed opt-in candidate selected"), Presented.Handle == OptIn);
	TestEqual(TEXT("opt-out then opt-in gives one actual notice"), Fixture.Notices->GetEntries().Num(), Before + 1);
	ASC.AbilityInputTagPressed(VBGameplayTags::Input_Execute);
	TestEqual(TEXT("next input adds only its two reports"), Log.GetCount(), 4);
	TestEqual(TEXT("no retained temporary listener duplicate"), Presentations, 2);
	const int32 BeforeDirect = Fixture.Notices->GetEntries().Num();
	ASC.TryActivateAbility(OptIn);
	TestEqual(TEXT("non-input activation failure still reports"), Log.GetCount(), 5);
	TestEqual(TEXT("non-input activation never presents"), Fixture.Notices->GetEntries().Num(), BeforeDirect);
	ASC.OnPlayerAbilityFailure.Remove(Observer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureLifetimeTest, "VB.Unit.AbilityFailure.ProducerLifetimeAndQueries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace VBFailureTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *Fixture.ASC;
	const auto Handle = ASC.GiveAbility(FGameplayAbilitySpec(UVBAbilityFailureOptInExecute::StaticClass(), 1));
	const auto* Ability = Cast<UVBGA_Execute>(ASC.FindAbilitySpecFromHandle(Handle)->Ability.Get());
	FFailureLog Log;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FGameplayTagContainer Reasons;
		TestFalse(TEXT("cooldown query rejects"),
			Ability->CanActivateAbility(Handle, ASC.AbilityActorInfo.Get(), nullptr, nullptr, &Reasons));
		TestTrue(TEXT("query explains cooldown"), Reasons.HasTagExact(VBGameplayTags::Ability_Fail_Cooldown));
	}

	ASC.RemoveActiveGameplayEffect(Fixture.Cooldown);
	FGameplayTagContainer Reasons;
	TestFalse(TEXT("no target query rejects"), Ability->CanActivateAbility(Handle, ASC.AbilityActorInfo.Get(), nullptr, nullptr, &Reasons));
	TestTrue(TEXT("query explains missing medical target"), Reasons.HasTagExact(VBGameplayTags::Ability_Fail_NoMedicalTarget));
	TestFalse(TEXT("null relevant tags are safe"),
		Ability->CanActivateAbility(Handle, ASC.AbilityActorInfo.Get(), nullptr, nullptr, nullptr));
	TestEqual(TEXT("pure queries never report"), Log.GetCount(), 0);
	const int32 Before = Fixture.Notices->GetEntries().Num();
	TestEqual(TEXT("pure queries never present"), Before, 0);
	Fixture.PC->OnRep_PlayerState();
	Fixture.PC->OnRep_PlayerState();
	ASC.AbilityInputTagPressed(VBGameplayTags::Input_Execute);
	TestEqual(TEXT("same-state signals keep one subscription"), Fixture.Notices->GetEntries().Num() - Before, 1);
	AVBPlayerState* Replacement = Fixture.World->SpawnActor<AVBPlayerState>();
	if (!TestNotNull(TEXT("replacement state"), Replacement))
	{
		return false;
	}

	Replacement->SetOwner(Fixture.PC);
	Fixture.PC->PlayerState = Replacement;
	Fixture.PC->OnRep_PlayerState();
	TestFalse(TEXT("old ASC delegate no longer references producer"), ASC.OnPlayerAbilityFailure.IsBoundToObject(Fixture.Producer));
	UVBAbilitySystemComponent* NewASC = Cast<UVBAbilitySystemComponent>(Replacement->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("replacement ASC"), NewASC))
	{
		return false;
	}

	NewASC->InitAbilityActorInfo(Replacement, Fixture.Pawn);
	NewASC->GiveAbility(FGameplayAbilitySpec(UVBAbilityFailureOptInExecute::StaticClass(), 1));
	FVBAbilityFailureReport Stale;
	Stale.Avatar = Fixture.Pawn;
	Stale.FailureTags.AddTag(VBGameplayTags::Ability_Fail_Unknown);
	const int32 BeforeReplace = Fixture.Notices->GetEntries().Num();
	ASC.OnPlayerAbilityFailure.Broadcast(Stale);
	TestEqual(TEXT("old ASC subscription removed"), Fixture.Notices->GetEntries().Num(), BeforeReplace);
	NewASC->AbilityInputTagPressed(VBGameplayTags::Input_Execute);
	TestEqual(TEXT("replacement ASC delivers one notice"), Fixture.Notices->GetEntries().Num(), BeforeReplace + 1);
	FVBAbilityFailureReport WrongAvatar = Stale;
	WrongAvatar.Avatar = Fixture.World->SpawnActor<APawn>();
	NewASC->OnPlayerAbilityFailure.Broadcast(WrongAvatar);
	TestEqual(TEXT("stale avatar never presents"), Fixture.Notices->GetEntries().Num(), BeforeReplace + 1);
	Fixture.StopProducer();
	TestFalse(TEXT("EndPlay removes ASC delegate"), NewASC->OnPlayerAbilityFailure.IsBoundToObject(Fixture.Producer));
	NewASC->OnPlayerAbilityFailure.Broadcast(Stale);
	TestEqual(TEXT("EndPlay unbinds replacement ASC"), Fixture.Notices->GetEntries().Num(), BeforeReplace + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureDataTest, "VB.Unit.AbilityFailure.MessageData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureDataTest::RunTest(const FString& Parameters)
{
	using namespace VBFailureTests;
	UVBAbilityFailureMessageData* Data = NewObject<UVBAbilityFailureMessageData>();
	AddRow(*Data, VBGameplayTags::Ability_Fail_Unknown, TEXT("fallback"));
	AddRow(*Data, VBGameplayTags::Ability_Fail_Cost, TEXT("cost"), 10);
	AddRow(*Data, VBGameplayTags::Ability_Fail_Cooldown, TEXT("cooldown"), 20);
	AddRow(*Data, FGameplayTag::RequestGameplayTag(TEXT("Ability.Fail")), TEXT("parent must not match"), 999);
	FGameplayTagContainer Reasons;
	Reasons.AddTag(VBGameplayTags::Ability_Fail_Cost);
	Reasons.AddTag(VBGameplayTags::Ability_Fail_Cooldown);
	const FVBAbilityFailureMessageRow* Found = Data->FindRow(Reasons);
	if (TestNotNull(TEXT("highest priority match"), Found))
	{
		TestEqual(TEXT("priority"), Found->Message.ToString(), FString(TEXT("cooldown")));
	}

	Data->Rows[2].Priority = 10;
	Swap(Data->Rows[1], Data->Rows[2]);
	Found = Data->FindRow(Reasons);
	if (TestNotNull(TEXT("tie match"), Found))
	{
		TestEqual(TEXT("lexical tie independent of row order"), Found->Message.ToString(), FString(TEXT("cooldown")));
	}

	Found = Data->FindRow(FGameplayTagContainer(VBGameplayTags::Ability_Fail_NoMedicalTarget));
	if (TestNotNull(TEXT("unmapped reason fallback"), Found))
	{
		TestEqual(TEXT("fallback text"), Found->Message.ToString(), FString(TEXT("fallback")));
	}
#if WITH_EDITOR
	FDataValidationContext PartialContext;
	TestTrue(TEXT("a reason without its own row is invalid"), Data->IsDataValid(PartialContext) == EDataValidationResult::Invalid);
	UVBAbilityFailureMessageData* Complete = MakeCompleteData();
	FDataValidationContext ValidContext;
	TestTrue(TEXT("a row for every reason passes validation"), Complete->IsDataValid(ValidContext) == EDataValidationResult::Valid);
	Complete->Rows.RemoveAll([](const FVBAbilityFailureMessageRow& Row) { return Row.FailureTag == VBGameplayTags::Ability_Fail_NoOwnedWeapon; });
	FDataValidationContext NoWeaponContext;
	TestTrue(TEXT("missing NoOwnedWeapon row is invalid"), Complete->IsDataValid(NoWeaponContext) == EDataValidationResult::Invalid);
	AddSilentRow(*Complete, VBGameplayTags::Ability_Fail_NoOwnedWeapon);
	FDataValidationContext SilentContext;
	TestTrue(TEXT("a silent row counts as a decision"), Complete->IsDataValid(SilentContext) == EDataValidationResult::Valid);
	const FVBAbilityFailureMessageRow* SilentRow = Complete->FindRow(FGameplayTagContainer(VBGameplayTags::Ability_Fail_NoOwnedWeapon));
	if (TestNotNull(TEXT("silent row is found, not treated as invalid data"), SilentRow))
	{
		TestTrue(TEXT("found row is silent"), SilentRow->bSilent);
	}

	Complete->Rows.Last().Message = FText::FromString(TEXT("ambiguous"));
	FDataValidationContext AmbiguousContext;
	TestTrue(TEXT("silent with a message is invalid"), Complete->IsDataValid(AmbiguousContext) == EDataValidationResult::Invalid);
#endif
	const FVBAbilityFailureMessageRow DuplicateRow = Data->Rows.Last();
	Data->Rows.Add(DuplicateRow);
	TestNull(TEXT("duplicate tags fail closed"), Data->FindRow(Reasons));
#if WITH_EDITOR
	FDataValidationContext DuplicateContext;
	TestTrue(TEXT("duplicate tags are invalid"), Data->IsDataValid(DuplicateContext) == EDataValidationResult::Invalid);
#endif
	Data->Rows.Pop();
	Data->Rows[1].Message = FText::GetEmpty();
	TestNull(TEXT("empty messages fail closed"), Data->FindRow(Reasons));
	Data->Rows[1].Message = FText::FromString(TEXT("restored"));
	Data->Rows[1].FailureTag = FGameplayTag();
	TestNull(TEXT("invalid tags fail closed"), Data->FindRow(Reasons));
	Data->Rows.RemoveAt(1);
	Data->Rows.RemoveAt(0);
	TestNull(TEXT("missing Unknown fails closed"), Data->FindRow(Reasons));
#if WITH_EDITOR
	FDataValidationContext MissingContext;
	TestTrue(TEXT("missing Unknown is invalid"), Data->IsDataValid(MissingContext) == EDataValidationResult::Invalid);
#endif
	return true;
}
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureProducerDataTest, "VB.Unit.AbilityFailure.ProducerNeedsMessages",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureProducerDataTest::RunTest(const FString& Parameters)
{
	UVBNoticeProducerComponent* Producer = NewObject<UVBNoticeProducerComponent>();
	FDataValidationContext EmptyContext;
	TestTrue(TEXT("no message table is invalid"),
		static_cast<const UObject*>(Producer)->IsDataValid(EmptyContext) == EDataValidationResult::Invalid);
	FVBAbilityFailureTestAccess::SetMessages(*Producer, VBFailureTests::MakeCompleteData());
	FDataValidationContext SetContext;
	TestTrue(TEXT("an assigned table is valid"),
		static_cast<const UObject*>(Producer)->IsDataValid(SetContext) == EDataValidationResult::Valid);
	return true;
}

// 저장된 실제 player controller 와 그 표를 읽는다 - 안 읽으면 표가 비어도 조용하다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureProductionAssetsTest, "VB.Unit.AbilityFailure.ProductionAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureProductionAssetsTest::RunTest(const FString& Parameters)
{
	const UClass* PCClass = StaticLoadClass(AVBPlayerController::StaticClass(), nullptr,
		TEXT("/Game/VB/Blueprints/GameModes/BP_VBPlayerController.BP_VBPlayerController_C"));
	const AVBPlayerController* PC = PCClass ? PCClass->GetDefaultObject<AVBPlayerController>() : nullptr;
	const UVBNoticeProducerComponent* Producer = PC ? PC->FindComponentByClass<UVBNoticeProducerComponent>() : nullptr;
	if (!TestNotNull(TEXT("BP_VBPlayerController NoticeProducer"), Producer))
	{
		return false;
	}

	FDataValidationContext ProducerContext;
	TestTrue(TEXT("NoticeProducer has its message table"),
		static_cast<const UObject*>(Producer)->IsDataValid(ProducerContext) == EDataValidationResult::Valid);
	const UVBAbilityFailureMessageData* Messages = FVBAbilityFailureTestAccess::Messages(*Producer);
	if (!TestNotNull(TEXT("message table"), Messages))
	{
		return false;
	}

	FDataValidationContext MessagesContext;
	TestTrue(TEXT("message table has a row for every reason"), Messages->IsDataValid(MessagesContext) == EDataValidationResult::Valid);
	// 무기 없이 공격을 누르면 아무것도 안 보인다.
	const FVBAbilityFailureMessageRow* NoWeapon = Messages->FindRow(FGameplayTagContainer(VBGameplayTags::Ability_Fail_NoOwnedWeapon));
	if (TestNotNull(TEXT("NoOwnedWeapon row"), NoWeapon))
	{
		TestTrue(TEXT("NoOwnedWeapon is silent"), NoWeapon->bSilent);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureSilentPressTest, "VB.Unit.AbilityFailure.SilentPress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureSilentPressTest::RunTest(const FString& Parameters)
{
	using namespace VBFailureTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	int32 Presentations = 0;
	const auto Presented = Fixture.ASC->OnPlayerAbilityFailure.AddLambda([&](const FVBAbilityFailureReport&) { ++Presentations; });
	const int32 Before = Fixture.Notices->GetEntries().Num();
	FFailureLog Log;
	Fixture.ASC->PresentPressFailure(VBGameplayTags::Ability_Fail_NoOwnedWeapon);
	TestEqual(TEXT("the press failure still reaches the producer"), Presentations, 1);
	TestEqual(TEXT("the press failure is still logged"), Log.GetCount(), 1);
	TestEqual(TEXT("a silent reason adds no notice"), Fixture.Notices->GetEntries().Num(), Before);
	Fixture.ASC->PresentPressFailure(VBGameplayTags::Ability_Fail_Unknown);
	TestEqual(TEXT("a reason with text still shows"), Fixture.Notices->GetEntries().Num(), Before + 1);
	Fixture.ASC->OnPlayerAbilityFailure.Remove(Presented);
	return true;
}

// Execute, Purify, 약공격은 스태미나를 안 쓴다(회피, 막기는 쓴다) - cost GE 가 몰래 돌아와 CheckCost 에서 막지 않게 지킨다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilityFailureNoStaminaCostTest, "VB.Unit.AbilityFailure.NoStaminaCost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAbilityFailureNoStaminaCostTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : { TEXT("/Game/VB/GAS/Abilities/BP_GA_Execute.BP_GA_Execute_C"),
		TEXT("/Game/VB/GAS/Abilities/BP_GA_Purify.BP_GA_Purify_C"),
		TEXT("/Game/VB/GAS/Abilities/BP_GA_LightAttack.BP_GA_LightAttack_C") })
	{
		const UClass* AbilityClass = StaticLoadClass(UGameplayAbility::StaticClass(), nullptr, Path);
		const UGameplayAbility* Ability = AbilityClass ? AbilityClass->GetDefaultObject<UGameplayAbility>() : nullptr;
		if (!TestNotNull(Path, Ability))
		{
			continue;
		}

		TestNull(FString::Printf(TEXT("%s has no cost effect"), Path), Ability->GetCostGameplayEffect());
	}

	return true;
}
#endif
#endif

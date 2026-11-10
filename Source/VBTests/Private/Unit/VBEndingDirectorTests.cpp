#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Campaign/VBCampaignStateComponent.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Ending/VBEndingCatalog.h"
#include "Narrative/Ending/VBEndingDirectorSubsystem.h"
#include "Narrative/Ending/VBEndingSettings.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "UObject/StrongObjectPtr.h"

namespace VBEndingTest
{
	FGameplayTag EndingDirectorTag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name)); }

	struct FFixture
	{
		FVBIsolatedGameWorld Scope;
		TStrongObjectPtr<UVBEndingCatalog> Catalog{NewObject<UVBEndingCatalog>()};
		TSoftObjectPtr<UVBEndingCatalog> PreviousCatalog = GetDefault<UVBEndingSettings>()->Catalog;
		UVBEndingDirectorSubsystem* Director = nullptr;
		UVBCampaignStateComponent* Campaign = nullptr;
		UVBStoryStateComponent* Story = nullptr;
		APlayerState* Player = nullptr;

		~FFixture() { GetMutableDefault<UVBEndingSettings>()->Catalog = PreviousCatalog; }

		bool Init(FAutomationTestBase& Test)
		{
			if (!Scope.Init(Test))
			{
				return false;
			}

			AGameStateBase* GameState = Scope.World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("game state"), GameState))
			{
				return false;
			}

			Scope.World->SetGameState(GameState);
			Campaign = NewObject<UVBCampaignStateComponent>(GameState);
			GameState->AddInstanceComponent(Campaign);
			Campaign->RegisterComponent();
			Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();
			Player = Scope.World->SpawnActor<APlayerState>();
			Director = Scope.World->GetSubsystem<UVBEndingDirectorSubsystem>();
			// 메모리에서만 CDO 를 바꾸고 나갈 때마다 되돌린다. config, 슬롯에는 쓰지 않는다
			GetMutableDefault<UVBEndingSettings>()->Catalog = Catalog.Get();
			return Test.TestNotNull(TEXT("ending director"), Director) && Test.TestNotNull(TEXT("player state"), Player);
		}

		FVBEndingDefinition& Add(FGameplayTag Ending, FGameplayTag RequiredFact = FGameplayTag())
		{
			FVBEndingDefinition& Row = Catalog->Endings.AddDefaulted_GetRef();
			Row.Ending = Ending;
			Row.ChoiceText = FText::FromString(TEXT("choose"));
			Row.ConsequenceText = FText::FromString(TEXT("consequence"));
			if (RequiredFact.IsValid())
			{
				FVBCondition_Fact Condition;
				Condition.Fact = RequiredFact;
				Row.EligibleWhen.Add(FInstancedStruct::Make(Condition));
			}

			return Row;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEndingDirectorChoiceTest, "VB.Unit.Ending.DirectorChoice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEndingDirectorChoiceTest::RunTest(const FString& Parameters)
{
	VBEndingTest::FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	using namespace VBEndingTest;
	const FGameplayTag Shared = EndingDirectorTag(TEXT("Ending.SharedRecovery"));
	const FGameplayTag Alone = EndingDirectorTag(TEXT("Ending.ReturnAlone"));
	const FGameplayTag Rescued = EndingDirectorTag(TEXT("Story.Test.ErrandDone"));
	const FGameplayTag Talked = EndingDirectorTag(TEXT("Story.Test.Talked"));
	const FGameplayTag Never = EndingDirectorTag(TEXT("Story.Test.Never"));
	Fixture.Add(Shared, Rescued).RememberFacts.AddTag(Talked);
	Fixture.Catalog->Endings[0].RememberFacts.AddTag(Never);
	Fixture.Add(Alone, Never);
	AddExpectedMessagePlain(TEXT("고를 수 있는 결말이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("empty eligible group refuses finale"), Fixture.Director->BeginFinale(Fixture.Player));
	TestFalse(TEXT("empty group has no attempt"), Fixture.Director->GetAttemptId().IsValid());
	Fixture.Story->SetFact(Rescued, true);
	if (!TestTrue(TEXT("eligible finale begins"), Fixture.Director->BeginFinale(Fixture.Player)))
	{
		return false;
	}

	const FGuid Attempt = Fixture.Director->GetAttemptId();
	TestTrue(TEXT("attempt id valid"), Attempt.IsValid());
	TestEqual(TEXT("only one offer"), Fixture.Director->GetOfferedEndings().Num(), 1);
	TestFalse(TEXT("second begin cannot replace an outstanding offer"), Fixture.Director->BeginFinale(Fixture.Player));
	TestFalse(TEXT("stale attempt rejected"), Fixture.Director->ChooseEnding(FGuid::NewGuid(), Shared));
	TestFalse(TEXT("tag outside the frozen group rejected"), Fixture.Director->ChooseEnding(Attempt, Alone));
	TestFalse(TEXT("invalid requests never record clear"), Fixture.Campaign->GetState().LastClear.IsSet());
	Fixture.Story->SetFact(Rescued, false);
	Fixture.Story->SetFact(Talked, true);
	Fixture.Story->SetFact(Never, true);
	Fixture.Catalog->Endings[0].RememberFacts.Reset();
	Fixture.Catalog->Endings[0].ChoiceText = FText::FromString(TEXT("changed after offer"));
	TestEqual(TEXT("displayed offer is a copy"), Fixture.Director->GetOfferedEndings()[0].ChoiceText.ToString(), FString(TEXT("choose")));
	TestFalse(TEXT("newly eligible ending is still outside the frozen group"), Fixture.Director->ChooseEnding(Attempt, Alone));
	const FGuid Run = Fixture.Campaign->GetState().RunId;
	if (!TestTrue(TEXT("offered ending survives changed facts and catalog"), Fixture.Director->ChooseEnding(Attempt, Shared)))
	{
		return false;
	}

	const FVBClearSummary First = Fixture.Campaign->GetState().LastClear;
	TestTrue(TEXT("cinematic-less ending records clear"), First.IsSet() && First.Ending == Shared);
	TestTrue(TEXT("remembers a fact enabled after offer, using frozen row"), First.Facts.HasTagExact(Talked));
	TestTrue(TEXT("summary reads completion-time facts"), First.Facts.HasTagExact(Never));
	TestFalse(TEXT("unlisted story fact is excluded"), First.Facts.HasTagExact(Rescued));
	TestEqual(TEXT("clear does not change run identity"), Fixture.Campaign->GetState().RunId, Run);
	TestFalse(TEXT("duplicate choice cannot complete twice"), Fixture.Director->ChooseEnding(Attempt, Shared));
	TestEqual(TEXT("duplicate keeps clear id"), Fixture.Campaign->GetState().LastClear.ClearId, First.ClearId);
	TestFalse(TEXT("completed attempt cannot be cancelled"), Fixture.Director->CancelFinale(Attempt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEndingDirectorCancelTest, "VB.Unit.Ending.DirectorCancel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEndingDirectorCancelTest::RunTest(const FString& Parameters)
{
	VBEndingTest::FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	const FGameplayTag Ending = VBEndingTest::EndingDirectorTag(TEXT("Ending.ReturnAlone"));
	Fixture.Add(Ending);
	if (!TestTrue(TEXT("begin"), Fixture.Director->BeginFinale(Fixture.Player)))
	{
		return false;
	}

	const FGuid Cancelled = Fixture.Director->GetAttemptId();
	TestFalse(TEXT("stale cancel ignored"), Fixture.Director->CancelFinale(FGuid::NewGuid()));
	TestTrue(TEXT("decline cancels the offer"), Fixture.Director->CancelFinale(Cancelled));
	TestTrue(TEXT("cancel clears offers"), Fixture.Director->GetOfferedEndings().IsEmpty());
	TestFalse(TEXT("cancel clears attempt"), Fixture.Director->GetAttemptId().IsValid());
	TestFalse(TEXT("cancel records no clear"), Fixture.Campaign->GetState().LastClear.IsSet());
	TestFalse(TEXT("cancelled choice rejected"), Fixture.Director->ChooseEnding(Cancelled, Ending));
	if (!TestTrue(TEXT("new attempt"), Fixture.Director->BeginFinale(Fixture.Player)))
	{
		return false;
	}

	TestNotEqual(TEXT("new id after cancel"), Fixture.Director->GetAttemptId(), Cancelled);
	const FGuid BeforeLoad = Fixture.Director->GetAttemptId();
	const TStrongObjectPtr<UVBSaveGame> Save(NewObject<UVBSaveGame>());
	Fixture.Story->ReadSave(*Save.Get(), FVBSaveContext());
	TestFalse(TEXT("save reload invalidates outstanding choice"), Fixture.Director->ChooseEnding(BeforeLoad, Ending));
	TestFalse(TEXT("reload records no clear"), Fixture.Campaign->GetState().LastClear.IsSet());
	if (!TestTrue(TEXT("new attempt after reload"), Fixture.Director->BeginFinale(Fixture.Player)))
	{
		return false;
	}

	const FGuid BeforeDestroy = Fixture.Director->GetAttemptId();
	Fixture.Player->Destroy();
	TestFalse(TEXT("destroyed instigator cannot complete"), Fixture.Director->ChooseEnding(BeforeDestroy, Ending));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEndingDirectorReceiptTest, "VB.Unit.Ending.DirectorReceipt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEndingDirectorReceiptTest::RunTest(const FString& Parameters)
{
	VBEndingTest::FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	const FGameplayTag Ending = VBEndingTest::EndingDirectorTag(TEXT("Ending.ReturnAlone"));
	const FGameplayTag Remember = VBEndingTest::EndingDirectorTag(TEXT("Story.Test.Talked"));
	const FGameplayTag Off = VBEndingTest::EndingDirectorTag(TEXT("Story.Test.Never"));
	Fixture.Add(Ending).RememberFacts.AddTag(Remember);
	Fixture.Catalog->Endings[0].RememberFacts.AddTag(Off);
	UVBCinematicDirectorSubsystem* Cinematic = Fixture.Scope.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	if (!TestNotNull(TEXT("cinematic director"), Cinematic))
	{
		return false;
	}
	// 받아들여진 세션 경계만 주입한다 - 실제 재생, 복원이 아니라 영수증 대응을 잰다
	const auto ArmReceipt = [&]() -> FGuid
	{
		if (!Fixture.Director->BeginFinale(Fixture.Player)) return {};
		Fixture.Director->Phase = UVBEndingDirectorSubsystem::EPhase::Playing;
		Fixture.Director->SelectedIndex = 0;
		Fixture.Director->PlaybackSessionId = FGuid::NewGuid();
		return Fixture.Director->PlaybackSessionId;
	};
	FGuid Session = ArmReceipt();
	if (!TestTrue(TEXT("admitted session fixture"), Session.IsValid()))
	{
		return false;
	}

	Cinematic->OnHandoffEnded.Broadcast({FGuid::NewGuid(), EVBCinematicEndType::Completed, EVBCinematicEndReason::None}, true);
	TestFalse(TEXT("another session cannot complete finale"), Fixture.Campaign->GetState().LastClear.IsSet());
	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Aborted, EVBCinematicEndReason::Cancelled}, false);
	TestFalse(TEXT("abort records no clear"), Fixture.Campaign->GetState().LastClear.IsSet());
	Session = ArmReceipt();
	if (!TestTrue(TEXT("second admitted session"), Session.IsValid()))
	{
		return false;
	}

	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Completed, EVBCinematicEndReason::None}, false);
	TestFalse(TEXT("playback completed without successful handoff records no clear"), Fixture.Campaign->GetState().LastClear.IsSet());
	Session = ArmReceipt();
	if (!TestTrue(TEXT("third admitted session"), Session.IsValid()))
	{
		return false;
	}

	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Aborted, EVBCinematicEndReason::Cancelled}, true);
	TestFalse(TEXT("aborted result cannot become a clear even with success flag"), Fixture.Campaign->GetState().LastClear.IsSet());
	Session = ArmReceipt();
	if (!TestTrue(TEXT("fourth admitted session"), Session.IsValid()))
	{
		return false;
	}

	const FGuid CancelledAttempt = Fixture.Director->GetAttemptId();
	TestTrue(TEXT("playing finale can be cancelled"), Fixture.Director->CancelFinale(CancelledAttempt));
	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Completed, EVBCinematicEndReason::None}, true);
	TestFalse(TEXT("late receipt after cancel records no clear"), Fixture.Campaign->GetState().LastClear.IsSet());
	Session = ArmReceipt();
	if (!TestTrue(TEXT("fifth admitted session"), Session.IsValid()))
	{
		return false;
	}

	Fixture.Story->SetFact(Remember, true);
	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Skipped, EVBCinematicEndReason::None}, true);
	const FVBClearSummary Completed = Fixture.Campaign->GetState().LastClear;
	TestTrue(TEXT("skip-to-end with successful handoff records clear"), Completed.IsSet());
	TestTrue(TEXT("completion keeps listed on fact"), Completed.Facts.HasTagExact(Remember));
	TestFalse(TEXT("completion drops listed off fact"), Completed.Facts.HasTagExact(Off));
	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Completed, EVBCinematicEndReason::None}, true);
	TestEqual(TEXT("duplicate receipt keeps clear id"), Fixture.Campaign->GetState().LastClear.ClearId, Completed.ClearId);
	Session = ArmReceipt();
	if (!TestTrue(TEXT("sixth admitted session"), Session.IsValid()))
	{
		return false;
	}

	Cinematic->OnHandoffEnded.Broadcast({Session, EVBCinematicEndType::Completed, EVBCinematicEndReason::None}, true);
	TestNotEqual(TEXT("a later successfully completed attempt gets a new clear"), Fixture.Campaign->GetState().LastClear.ClearId,
		Completed.ClearId);
	return true;
}
#endif

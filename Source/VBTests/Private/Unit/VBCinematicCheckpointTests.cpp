#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerRules.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBSaveGateTestAccess.h"
#include "VBCinematicDefinition.h"

// 재생과 복귀가 끝난 경계만 구성한다. 카메라와 입력 복구 시험은 별도다.
class FVBCinematicHandoffTestAccess
{
public:
	static void Close(UVBCinematicDirectorSubsystem& D, APawn& Pawn, APlayerState& State,
		UVBCinematicDefinition& Definition, TSharedPtr<FVBCinematicGameLease> Lease,
		EVBCinematicEndType Type, const TArray<FInstancedStruct>& Effects,
		FVBOnDeferredEffectClosed Closed = {}, bool bEffectsLanded = true)
	{
		TGuardValue<bool> Processing(D.bProcessing, true);
		D.RequestId = FGuid::NewGuid();
		D.AttemptId = UVBSaveGameSubsystem::Get(&Pawn)->GetGameStartContext().AttemptId;
		D.ActivePawn = &Pawn;
		D.ActiveController = Cast<APlayerController>(Pawn.GetController());
		D.ActiveInstigator = &State;
		D.ActiveDefinition = &Definition;
		D.Lease = MoveTemp(Lease);
		D.SessionId = FGuid::NewGuid();
		D.EndResult = FVBCinematicEndResult{D.SessionId, Type, EVBCinematicEndReason::None};
		D.bAdmitted = true;
		D.bRestorationConfirmed = true;
		D.HandoffEffects = Effects;
		D.ActiveOnClosed = MoveTemp(Closed);
		D.CloseRequest(Type == EVBCinematicEndType::Completed || Type == EVBCinematicEndType::Skipped, bEffectsLanded);
		D.ReleaseRequestData();
	}
	static void CloseAgain(UVBCinematicDirectorSubsystem& D) { D.CloseRequest(true); }
};

namespace VBCinematicCheckpointTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	class FReleaseLease final : public FVBCinematicGameLease
	{
	public:
		explicit FReleaseLease(UVBSaveGameSubsystem& InSave) : Save(InSave), Hold(Save.BeginSaveHold(TEXT("CheckpointTest"))) {}
		virtual bool IsSaveHeld() const override { return Save.IsSaveHoldActive(Hold); }
		virtual void RestoreGameplay() override {}
		virtual bool IsGameplayRestored() const override { return true; }
		virtual void Finish(bool) override { if (Hold.IsValid()) { Save.EndSaveHold(Hold); Hold = {}; } }
		virtual ~FReleaseLease() override { Finish(false); }
	private:
		UVBSaveGameSubsystem& Save;
		FVBSaveHoldToken Hold;
	};

	struct FCheckpointWorld
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		UVBSaveGameSubsystem* Save = nullptr;
		APlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		APlayerState* State = nullptr;
		AActor* Marker = nullptr;
		UVBStoryStateComponent* Story = nullptr;
		int32 Dispatches = 0;
		bool bDispatchHeld = false;
		bool bSavedFact = false;
		FGameplayTag Done = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.ErrandDone"));

		bool Init(FAutomationTestBase& Test)
		{
			UGameInstance* Instance = nullptr;
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game && Context.OwningGameInstance && Context.World())
					{
						Instance = Context.OwningGameInstance;
						break;
					}
				}
			}
			if (!Test.TestNotNull(TEXT("headless game instance; no user PIE"), Instance)) return false;
			Save = Instance->GetSubsystem<UVBSaveGameSubsystem>();
			if (!Test.TestNotNull(TEXT("real save subsystem"), Save)) return false;
			FVBSaveGateTestAccess::SetReadDisk(*Save, [](const FString&, int32) { return false; },
				[](const FString&, int32) -> USaveGame* { return nullptr; });
			FVBSaveGateTestAccess::SetDispatch(*Save, [this](UVBSaveGame* Snapshot, const FString& Slot, int32 User,
				const FAsyncSaveGameToSlotDelegate& Completion)
			{
				++Dispatches;
				bDispatchHeld = Save->IsGameSaveHeld();
				bSavedFact = Snapshot->Progress.Story.HasFact(Done);
				Completion.ExecuteIfBound(Slot, User, true);
			});
			Save->StartNewGame();
			const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (!Test.TestNotNull(TEXT("owned isolated world"), World)) return false;
			Package.Reset(World->GetPackage());
			World->SetGameInstance(Instance);
			World->InitializeActorsForPlay(FURL());
			Controller = World->SpawnActor<APlayerController>();
			State = World->SpawnActor<APlayerState>();
			Pawn = World->SpawnActor<APawn>();
			Marker = World->SpawnActor<AActor>();
			AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
			if (!Test.TestTrue(TEXT("fixture actors"), Controller && State && Pawn && Marker && GameState)) return false;
			Controller->PlayerState = State;
			State->SetOwner(Controller);
			Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
			Controller->Possess(Pawn);
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));
			LocalPlayer->SwitchController(Controller);
			World->SetGameState(GameState);
			Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();
			UVBSaveParticipantSubsystem::Register(Story);
			UVBPlayerRestoreComponent* Restore = NewObject<UVBPlayerRestoreComponent>(Pawn);
			Pawn->AddInstanceComponent(Restore);
			Restore->RegisterComponent();
			UVBSaveParticipantSubsystem::Register(Restore);
			return Test.TestTrue(TEXT("actual possession"), Pawn->GetPlayerState() == State);
		}
		~FCheckpointWorld()
		{
			if (Save) FVBSaveGateTestAccess::RestoreDisk(*Save);
			if (LocalPlayer.IsValid())
			{
				if (Controller && Controller->Player == LocalPlayer.Get()) Controller->Player = nullptr;
				LocalPlayer->PlayerController = nullptr;
				LocalPlayer.Reset();
			}
			if (World) { World->DestroyWorld(false); Package->SetDirtyFlag(false); }
		}
		FInstancedStruct Checkpoint() const
		{
			FVBEffect_SaveCheckpoint Effect;
			Effect.RestoreAt = Marker;
			return FInstancedStruct::Make(Effect);
		}
	};
	FInstancedStruct Fact(FGameplayTag Tag)
	{
		FVBEffect_SetFact Effect;
		Effect.Fact = Tag;
		Effect.bOn = true;
		return FInstancedStruct::Make(Effect);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicCheckpointFailureTest,
	"VB.Unit.Cinematic.Checkpoint.FailureReported", VBCinematicCheckpointTest::Flags)
bool FVBCinematicCheckpointFailureTest::RunTest(const FString& Parameters)
{
	using namespace VBCinematicCheckpointTest;
	AddExpectedMessagePlain(TEXT("효과(체크포인트 저장)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("해내지 못"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("Held"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	const TArray<FInstancedStruct> Missing = {FInstancedStruct::Make(FVBEffect_SaveCheckpoint())};
	TestTrue(TEXT("missing context reports failure"), VBRules::ApplyAllUntilFailure(Missing,
		[](FVBRuleContext& Out) { Out = {}; return true; }) == VBRules::EApplyResult::Failed);
	FCheckpointWorld F;
	if (!F.Init(*this)) return false;
	FReleaseLease Lease(*F.Save);
	if (!TestTrue(TEXT("real save hold"), Lease.IsSaveHeld())) return false;
	const TArray<FInstancedStruct> Effects = {F.Checkpoint(), Fact(F.Done)};
	auto Prepare = [&](FVBRuleContext& Out) { Out = FVBRuleContext::Make(F.World, F.State); return true; };
	TestTrue(TEXT("Held checkpoint fails through real save API"),
		VBRules::ApplyAllUntilFailure(Effects, Prepare) == VBRules::EApplyResult::Failed);
	TestEqual(TEXT("no transport while held"), F.Dispatches, 0);
	TestFalse(TEXT("refused checkpoint stops next effect"), F.Story->HasFact(F.Done));
	Lease.Finish(true);
	TestTrue(TEXT("same effect passes after release"),
		VBRules::ApplyAllUntilFailure(Effects, Prepare) == VBRules::EApplyResult::Landed);
	TestEqual(TEXT("one admitted transport"), F.Dispatches, 1);
	TestFalse(TEXT("transport observes released hold"), F.bDispatchHeld);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicCheckpointHandoffTest,
	"VB.Unit.Cinematic.Checkpoint.Handoff", VBCinematicCheckpointTest::Flags)
bool FVBCinematicCheckpointHandoffTest::RunTest(const FString& Parameters)
{
	using namespace VBCinematicCheckpointTest;
	const EVBCinematicEndType Types[] = {EVBCinematicEndType::Completed, EVBCinematicEndType::Skipped,
		EVBCinematicEndType::Aborted, EVBCinematicEndType::Failed, EVBCinematicEndType::Completed, EVBCinematicEndType::Completed};
	for (int32 Case = 0; Case < UE_ARRAY_COUNT(Types); ++Case)
	{
		const EVBCinematicEndType Type = Types[Case];
		const bool bEffectsLanded = Case != 4;
		const bool bHasHandoffEffects = Case != 5;
		FCheckpointWorld F;
		if (!F.Init(*this)) return false;
		auto* Director = F.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
		if (!TestNotNull(TEXT("real director"), Director)) return false;
		const TStrongObjectPtr<UVBCinematicDefinition> Definition(NewObject<UVBCinematicDefinition>());
		const auto Lease = MakeShared<FReleaseLease>(*F.Save);
		if (!TestTrue(TEXT("hold before close"), Lease->IsSaveHeld())) return false;
		int32 ClosedCount = 0;
		const bool bSuccess = (Type == EVBCinematicEndType::Completed || Type == EVBCinematicEndType::Skipped) && bEffectsLanded;
		const int32 ExpectedSaves = bSuccess && bHasHandoffEffects ? 1 : 0;
		const TArray<FInstancedStruct> Effects = bHasHandoffEffects ? TArray<FInstancedStruct>{F.Checkpoint()} : TArray<FInstancedStruct>{};
		if (Case == 0)
		{
			const auto Refused = Director->RequestPlay(nullptr, F.State, TEXT("Player"), {}, nullptr, {}, Effects);
			TestTrue(TEXT("refused request is not admitted"), Refused.Status == EVBGameCinematicStartStatus::InvalidContext);
			TestEqual(TEXT("refused request runs no handoff effects"), F.Dispatches, 0);
			const auto Invalid = Director->RequestPlay(Definition.Get(), F.State, TEXT("Player"), {}, nullptr, {},
				{FInstancedStruct::Make(FVBCondition_Fact())});
			TestTrue(TEXT("non-effect handoff list is refused before admission"), Invalid.Status == EVBGameCinematicStartStatus::InvalidContext);
			TestTrue(TEXT("refusal preserves caller's existing hold"), Lease->IsSaveHeld());
		}
		FVBCinematicHandoffTestAccess::Close(*Director, *F.Pawn, *F.State, *Definition, Lease, Type, Effects,
			FVBOnDeferredEffectClosed::CreateLambda([&](bool bSucceeded)
			{
				++ClosedCount;
				TestEqual(TEXT("close preserves end semantics"), bSucceeded, bSuccess);
				TestFalse(TEXT("close callback sees released lease"), F.Save->IsGameSaveHeld());
				if (bSucceeded) F.Story->SetFact(F.Done, true);
			}), bEffectsLanded);
		TestEqual(TEXT("only explicit successful handoff list saves"), F.Dispatches, ExpectedSaves);
		TestEqual(TEXT("snapshot includes deferred OnceFact"), F.bSavedFact, ExpectedSaves != 0);
		TestEqual(TEXT("one close callback"), ClosedCount, 1);
		TestFalse(TEXT("never submits under hold"), F.bDispatchHeld);
		FVBCinematicHandoffTestAccess::CloseAgain(*Director);
		TestEqual(TEXT("duplicate close does not repeat save"), F.Dispatches, ExpectedSaves);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicCheckpointInvalidationTest,
	"VB.Unit.Cinematic.Checkpoint.ContextInvalidation", VBCinematicCheckpointTest::Flags)
bool FVBCinematicCheckpointInvalidationTest::RunTest(const FString& Parameters)
{
	using namespace VBCinematicCheckpointTest;
	FCheckpointWorld F;
	if (!F.Init(*this)) return false;
	auto* Director = F.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	if (!TestNotNull(TEXT("director"), Director)) return false;
	const TStrongObjectPtr<UVBCinematicDefinition> Definition(NewObject<UVBCinematicDefinition>());
	const auto Lease = MakeShared<FReleaseLease>(*F.Save);
	const FGameplayTag Changed = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.Talked"));
	const FDelegateHandle Handle = F.Story->OnStoryStateChanged.AddLambda([&]()
	{
		if (F.Story->HasFact(Changed)) F.Save->StartNewGame();
	});
	AddExpectedError(TEXT("시네마틱 복귀 후 효과"), EAutomationExpectedErrorFlags::Contains, 1);
	FVBCinematicHandoffTestAccess::Close(*Director, *F.Pawn, *F.State, *Definition, Lease,
		EVBCinematicEndType::Completed, {Fact(Changed), F.Checkpoint()});
	F.Story->OnStoryStateChanged.Remove(Handle);
	TestTrue(TEXT("first post-effect ran"), F.Story->HasFact(Changed));
	TestEqual(TEXT("changed attempt blocks next effect"), F.Dispatches, 0);
	TestFalse(TEXT("receipt cannot cross attempts"), Director->HasCompletedFor(F.Pawn, Definition.Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicCheckpointPostFailureTest,
	"VB.Unit.Cinematic.Checkpoint.PostFailure", VBCinematicCheckpointTest::Flags)
bool FVBCinematicCheckpointPostFailureTest::RunTest(const FString& Parameters)
{
	using namespace VBCinematicCheckpointTest;
	FCheckpointWorld F;
	if (!F.Init(*this)) return false;
	auto* Director = F.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	if (!TestNotNull(TEXT("director"), Director)) return false;
	const TStrongObjectPtr<UVBCinematicDefinition> Definition(NewObject<UVBCinematicDefinition>());
	const auto Lease = MakeShared<FReleaseLease>(*F.Save);
	FVBSaveHoldToken OtherHold;
	AddExpectedError(TEXT("시네마틱 복귀 후 효과"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Held"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("효과(체크포인트 저장)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("해내지 못"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FVBCinematicHandoffTestAccess::Close(*Director, *F.Pawn, *F.State, *Definition, Lease,
		EVBCinematicEndType::Completed, {F.Checkpoint(), Fact(F.Done)},
		FVBOnDeferredEffectClosed::CreateLambda([&](bool) { OtherHold = F.Save->BeginSaveHold(TEXT("OtherOwner")); }));
	TestEqual(TEXT("new owner's hold prevents transport"), F.Dispatches, 0);
	TestFalse(TEXT("failure stops next effect"), F.Story->HasFact(F.Done));
	TestTrue(TEXT("never releases another owner's hold"), F.Save->IsSaveHoldActive(OtherHold));
	F.Save->EndSaveHold(OtherHold);
	return true;
}
#endif

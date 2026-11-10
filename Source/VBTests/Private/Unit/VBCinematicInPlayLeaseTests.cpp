#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Cinematic/VBCinematicInPlayLease.h"
#include "Narrative/Cinematic/VBCinematicInPlayPolicy.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 플레이 중 장면(영역 트리거의 PlayCinematic 효과)은 진행 중 정책의 임대가 필요하다: 감독의 진행 중 칸을 정책이 쥐고,
//   조종 중이 아니면 거절하며, 임대 동안 저장, 준비, 입력을 막고 장면이 끝나면 성패와 무관하게 셋을 푼다.
//   대조: 정책 설치 호출을 빼면 Provider 줄, 임대의 준비 검사를 빼면 Gate 줄이 실패한다.
class FVBInPlayLeaseTestAccess
{
public:
	static bool TakeHolds(FVBInPlayCinematicLease& Lease) { return Lease.TakeHolds(); }
	static bool HasInput(const FVBInPlayCinematicLease& Lease) { return Lease.InputGuard.IsValid(); }
};

namespace
{
	constexpr EAutomationTestFlags InPlayLeaseFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	uint32 InPlayBit(EVBPlayerPreparationReason Reason) { return static_cast<uint32>(Reason); }

	// 첫 장면 임대 시험과 같은 격리 모양: 자체 게임 인스턴스, 세이브 서브시스템을 가진 게임 월드, 준비 컴포넌트(초기화됐지만 Ready 아님)를 가진 폰을 빙의한 로컬 컨트롤러.
	struct FInPlayLeaseFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
		TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		UVBPlayerReadinessComponent* Readiness = nullptr;
		TSharedPtr<FVBInPlayCinematicLease> Lease;

		~FInPlayLeaseFixture()
		{
			Lease.Reset();
			if (LocalPlayer.IsValid())
			{
				APlayerController* BoundController = LocalPlayer->PlayerController;
				if (IsValid(BoundController) && BoundController->Player == LocalPlayer.Get())
				{
					BoundController->Player = nullptr;
				}

				LocalPlayer->PlayerController = nullptr;
				LocalPlayer.Reset();
			}

			if (!World)
			{
				return;
			}

			const TStrongObjectPtr<UPackage> Package(World->GetPackage());
			World->DestroyWorld(false);
			Package->SetDirtyFlag(false);
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("engine"), GEngine))
			{
				return false;
			}

			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			World->SetGameInstance(Instance.Get());
			Controller = World->SpawnActor<APlayerController>();
			Pawn = World->SpawnActor<APawn>();
			if (!Test.TestNotNull(TEXT("controller"), Controller) || !Test.TestNotNull(TEXT("pawn"), Pawn))
			{
				return false;
			}

			Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
			Controller->Possess(Pawn);
			Save->StartNewGame();
			Readiness = NewObject<UVBPlayerReadinessComponent>(Pawn);
			Pawn->AddInstanceComponent(Readiness);
			Readiness->RegisterComponent();
			Readiness->BeginInitialization(Save->GetGameStartContext().AttemptId, EVBGameStartIntent::NewGame);
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
			LocalPlayer->SwitchController(Controller);
			Lease = MakeShared<FVBInPlayCinematicLease>(World, Controller, Save.Get());
			return Test.TestTrue(TEXT("lease context is current"), Lease->IsCurrent());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInPlayLeaseProviderTest, "VB.Unit.Cinematic.InPlay.Provider", InPlayLeaseFlags)
bool FVBInPlayLeaseProviderTest::RunTest(const FString& Parameters)
{
	FInPlayLeaseFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCinematicDirectorSubsystem* Director = F.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	if (!TestNotNull(TEXT("director in a game world"), Director)
		|| !TestNotNull(TEXT("in-play policy in a game world"), F.World->GetSubsystem<UVBCinematicInPlayPolicy>()))
	{
		return false;
	}

	UObject* Intruder = GetTransientPackage();
	TestFalse(TEXT("the in-play slot is already held by the in-play policy"), Director->InstallInPlaySafetyProvider(Intruder,
		FVBCinematicAcquireSafety::CreateLambda([](APlayerController*, FGuid) { return TSharedPtr<FVBCinematicGameLease>(); })));
	TestEqual(TEXT("refusals log by name"), FString(LexToString(EVBGameCinematicStartStatus::SafetyUnavailable)),
		FString(TEXT("SafetyUnavailable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInPlayLeaseGateTest, "VB.Unit.Cinematic.InPlay.Gate", InPlayLeaseFlags)
bool FVBInPlayLeaseGateTest::RunTest(const FString& Parameters)
{
	FInPlayLeaseFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	TestFalse(TEXT("fixture player is not in control (initialization not ended, no UI source)"),
		F.Readiness->Query().State == EVBPlayerPreparationState::Ready);
	TestFalse(TEXT("not in control: no lease"), F.Lease->Acquire());
	TestFalse(TEXT("refused lease holds no save"), F.Save->IsGameSaveHeld());
	TestFalse(TEXT("refused lease pushes no input guard"), FVBInPlayLeaseTestAccess::HasInput(*F.Lease));
	TestEqual(TEXT("refused lease adds no preparation hold"),
		F.Readiness->Query().BlockedReasons & InPlayBit(EVBPlayerPreparationReason::ExternalHold), 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInPlayLeaseHoldsTest, "VB.Unit.Cinematic.InPlay.Holds", InPlayLeaseFlags)
bool FVBInPlayLeaseHoldsTest::RunTest(const FString& Parameters)
{
	FInPlayLeaseFixture F;
	if (!F.Initialize(*this) || !TestTrue(TEXT("holds taken"), FVBInPlayLeaseTestAccess::TakeHolds(*F.Lease)))
	{
		return false;
	}

	TestTrue(TEXT("holding: save held"), F.Lease->IsSaveHeld());
	TestTrue(TEXT("holding: input guard pushed"), FVBInPlayLeaseTestAccess::HasInput(*F.Lease));
	TestTrue(TEXT("holding: the player is held"),
		(F.Readiness->Query().BlockedReasons & InPlayBit(EVBPlayerPreparationReason::ExternalHold)) != 0);
	TestFalse(TEXT("not restored before the scene asks"), F.Lease->IsGameplayRestored());
	F.Lease->Finish(false);
	TestFalse(TEXT("finished (even aborted): save released - a scene inside play never keeps saving blocked"), F.Save->IsGameSaveHeld());
	TestFalse(TEXT("finished: input guard gone"), FVBInPlayLeaseTestAccess::HasInput(*F.Lease));
	TestEqual(TEXT("finished: player hold gone"),
		F.Readiness->Query().BlockedReasons & InPlayBit(EVBPlayerPreparationReason::ExternalHold), 0u);
	F.Lease->Finish(true);
	TestTrue(TEXT("finish is idempotent"), F.Lease->IsFinished());
	return true;
}
#endif

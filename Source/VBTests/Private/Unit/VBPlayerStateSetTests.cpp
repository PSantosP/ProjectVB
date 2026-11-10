#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Player/VBPlayerController.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// PlayerState 를 쓰는 화면은 PC 의 PlayerState 정해짐 신호를 기다린다. 원격 클라처럼 PlayerState 가 늦게 오는 순서를 시험 월드에서 만든다.
namespace
{
	constexpr EAutomationTestFlags PlayerStateSetFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FPlayerStateSetFixture
	{
		UWorld* World = nullptr;
		AVBPlayerController* Controller = nullptr;

		~FPlayerStateSetFixture()
		{
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
			if (!Test.TestNotNull(TEXT("Fixture.EngineAvailable"), GEngine))
			{
				return false;
			}

			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("Owned fixture world"), World))
			{
				return false;
			}
			// 게임 모드가 없으면 InitPlayerState 가 PlayerState 를 만들지 않는다 - PlayerState 가 아직 안 온 PC 가 된다.
			Controller = World->SpawnActor<AVBPlayerController>();
			return Test.TestNotNull(TEXT("Controller"), Controller)
				&& Test.TestNull(TEXT("Fixture.NoPlayerStateYet"), Controller->PlayerState.Get());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayerStateSetLateArrivalTest, "VB.Unit.Player.PlayerStateSet.LateArrival", PlayerStateSetFlags)
bool FVBPlayerStateSetLateArrivalTest::RunTest(const FString& Parameters)
{
	FPlayerStateSetFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBPlayerController* PC = Fixture.Controller;

	int32 EarlyCalls = 0;
	APlayerState* EarlySeen = nullptr;
	const FDelegateHandle EarlyHandle = PC->CallAndRegister_OnPlayerStateSet(FVBOnPlayerStateSet::FDelegate::CreateLambda(
		[&EarlyCalls, &EarlySeen](APlayerState* PlayerState) { ++EarlyCalls; EarlySeen = PlayerState; }));
	TestEqual(TEXT("registering before PlayerState does not call"), EarlyCalls, 0);

	// 복제로 늦게 도착: 값이 들어오고 OnRep 이 불린다.
	APlayerState* Arrived = Fixture.World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("PlayerState spawned"), Arrived))
	{
		return false;
	}

	PC->SetPlayerState(Arrived);
	PC->OnRep_PlayerState();
	TestEqual(TEXT("late PlayerState calls the early listener once"), EarlyCalls, 1);
	TestTrue(TEXT("listener receives the arrived PlayerState"), EarlySeen == Arrived);

	int32 LateCalls = 0;
	const FDelegateHandle LateHandle = PC->CallAndRegister_OnPlayerStateSet(FVBOnPlayerStateSet::FDelegate::CreateLambda(
		[&LateCalls](APlayerState*) { ++LateCalls; }));
	TestEqual(TEXT("registering after PlayerState calls immediately once"), LateCalls, 1);
	TestEqual(TEXT("immediate call is only for the new listener"), EarlyCalls, 1);

	// 해제한 쪽은 다시 불리지 않는다(화면이 꺼지면 해제한다).
	PC->UnregisterOnPlayerStateSet(EarlyHandle);
	PC->OnRep_PlayerState();
	TestEqual(TEXT("unregistered listener is not called again"), EarlyCalls, 1);
	TestEqual(TEXT("registered listener is called on the next set"), LateCalls, 2);

	// 비워지는 쪽은 알리지 않는다.
	PC->SetPlayerState(nullptr);
	PC->OnRep_PlayerState();
	TestEqual(TEXT("clearing PlayerState does not broadcast"), LateCalls, 2);

	PC->UnregisterOnPlayerStateSet(LateHandle);
	return true;
}

#endif

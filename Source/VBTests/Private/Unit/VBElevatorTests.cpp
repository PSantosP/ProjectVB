#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Interaction/VBElevator.h"
#include "Interaction/VBTimedMove.h"
#include "Interaction/VBInteractableComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBElevatorAlphaTest, "VB.Unit.Interaction.Elevator.Alpha",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBElevatorAlphaTest::RunTest(const FString& Parameters)
{
	// 아래(0) -> 위(1), 4초짜리 움직임이 서버 시각 10초에 출발.
	FVBTimedMove Up;
	Up.FromAlpha = 0.0f;
	Up.ToAlpha = 1.0f;
	Up.ServerStartTime = 10.0;
	Up.Duration = 4.0f;
	TestEqual(TEXT("before start = from"), VBTimedMove::EvaluateAlpha(Up, 9.0), 0.0f);
	TestEqual(TEXT("start = from"), VBTimedMove::EvaluateAlpha(Up, 10.0), 0.0f);
	TestTrue(TEXT("half time = half way (smoothstep is symmetric)"),
		FMath::IsNearlyEqual(VBTimedMove::EvaluateAlpha(Up, 12.0), 0.5f, 1e-4f));
	TestTrue(TEXT("eases in: a quarter of the time covers less than a quarter of the way"), VBTimedMove::EvaluateAlpha(Up, 11.0) < 0.25f);
	TestEqual(TEXT("end = to"), VBTimedMove::EvaluateAlpha(Up, 14.0), 1.0f);
	TestEqual(TEXT("after end stays at to"), VBTimedMove::EvaluateAlpha(Up, 99.0), 1.0f);

	// 멈춤은 FromAlpha 에 선다(시각이 흘러도 앞으로 가지 않는다).
	FVBTimedMove Stopped = Up;
	Stopped.FromAlpha = 0.4f;
	Stopped.bStopped = true;
	TestEqual(TEXT("stopped holds its alpha"), VBTimedMove::EvaluateAlpha(Stopped, 13.0), 0.4f);
	TestEqual(TEXT("stopped holds its alpha later too"), VBTimedMove::EvaluateAlpha(Stopped, 50.0), 0.4f);

	// 되돌림: 중간(0.6)에서 아래로 - 출발 순간은 그 자리다(튀지 않는다).
	FVBTimedMove Back;
	Back.FromAlpha = 0.6f;
	Back.ToAlpha = 0.0f;
	Back.ServerStartTime = 20.0;
	Back.Duration = VBTimedMove::TripDuration(0.6f, 0.0f, 800.0f, 200.0f);
	TestTrue(TEXT("reversal duration = remaining distance / speed"), FMath::IsNearlyEqual(Back.Duration, 2.4f, 1e-4f));
	TestEqual(TEXT("reversal starts where it was"), VBTimedMove::EvaluateAlpha(Back, 20.0), 0.6f);

	// 중간 출발의 끝은 목표값과 정확히 같아야 한다 - 한 ulp 라도 빗나가면 도착이 안 나서 저장 막기가 안 풀린다.
	FVBTimedMove Resume;
	Resume.FromAlpha = 0.37f;
	Resume.ToAlpha = 1.0f;
	Resume.ServerStartTime = 30.0;
	Resume.Duration = VBTimedMove::TripDuration(0.37f, 1.0f, 800.0f, 200.0f);
	TestTrue(TEXT("mid-trip resume ends exactly at the stop"), VBTimedMove::EvaluateAlpha(Resume, 30.0 + Resume.Duration) == 1.0f);
	TestTrue(TEXT("and stays exactly there"), VBTimedMove::EvaluateAlpha(Resume, 99.0) == 1.0f);

	// 0초짜리(세이브 복원, 같은 층)는 바로 끝 자리다.
	FVBTimedMove Snap;
	Snap.FromAlpha = 1.0f;
	Snap.ToAlpha = 1.0f;
	TestEqual(TEXT("zero duration = to"), VBTimedMove::EvaluateAlpha(Snap, 0.0), 1.0f);
	TestEqual(TEXT("full trip 800cm at 200cm/s"), VBTimedMove::TripDuration(0.0f, 1.0f, 800.0f, 200.0f), 4.0f);
	TestEqual(TEXT("zero speed never divides"), VBTimedMove::TripDuration(0.0f, 1.0f, 800.0f, 0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBElevatorRequestTest, "VB.Unit.Interaction.Elevator.RequestNeedsSaveHold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBElevatorRequestTest::RunTest(const FString& Parameters)
{
	// 게임 인스턴스가 없는 월드 = 저장 하위 시스템이 없다 = 저장 막기를 얻을 수 없다. 그러면 원하는 층이 바뀌면 안 된다.
	AddExpectedMessagePlain(TEXT("저장 막기를 얻지 못해"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AVBElevator* Elevator = World->SpawnActor<AVBElevator>();
	if (!TestNotNull(TEXT("elevator"), Elevator))
	{
		return false;
	}

	const UVBInteractableComponent* Panel = Elevator->FindComponentByClass<UVBInteractableComponent>();
	TestFalse(TEXT("starts at the lower stop"), Panel->IsActivated());
	TestFalse(TEXT("request refused without a save hold"), Elevator->RequestStop(true));
	TestFalse(TEXT("desired stop unchanged after the refusal"), Panel->IsActivated());
	TestFalse(TEXT("not moving after the refusal"), Elevator->IsMoving());
	TestFalse(TEXT("no character, no moving base"), AVBElevator::IsBaseMoving(nullptr));
	return true;
}

#endif

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimeControl/VBTimeDilationSubsystem.h"

// 시간 조정 서브시스템의 순서 요청(정확한 패링 슬로우).
// 만료는 타이머를 기다리지 않고 직접 일으킨다 - 타이머 관리자는 한 프레임에 한 번만 돌아 한 시험 안에서 여러 번 못 돌린다.
// 실초 만료(게임 시간 = 실초 x 배율)는 기존 PushTimed 의 계약이라 여기서 다시 재지 않는다.

class FVBTimeDilationTestAccess
{
public:
	static void Expire(UVBTimeDilationSubsystem& Time, FName Handle) { Time.OnRequestExpired(Handle); }
	static void ExpireActor(UVBTimeDilationSubsystem& Time, TWeakObjectPtr<AActor> Actor, FName Handle) { Time.OnActorRequestExpired(Actor, Handle); }
	static bool HasActorRequests(const UVBTimeDilationSubsystem& Time, TWeakObjectPtr<AActor> Actor) { return Time.ActorRequests.Contains(Actor); }
};

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTimeDilationSequenceTest, "VB.Unit.TimeControl.Sequence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTimeDilationSequenceTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UVBTimeDilationSubsystem* Time = World->GetSubsystem<UVBTimeDilationSubsystem>();
	if (!TestNotNull(TEXT("time dilation subsystem"), Time))
	{
		return false;
	}

	auto Step = [](float Scale, float RealDuration)
	{
		FVBTimeDilationStep Result;
		Result.Scale = Scale;
		Result.RealDuration = RealDuration;
		return Result;
	};
	const FName Parry(TEXT("Parry"));
	const TArray<FVBTimeDilationStep> Steps = { Step(0.2f, 0.5f), Step(0.5f, 0.4f) };

	// 단계대로 넘어가고 마지막 뒤에 정상.
	Time->PushSequence(Parry, Steps);
	TestEqual(TEXT("first step applies"), Time->GetAppliedScale(), 0.2f);
	FVBTimeDilationTestAccess::Expire(*Time, Parry);
	TestEqual(TEXT("second step follows without a gap"), Time->GetAppliedScale(), 0.5f);
	FVBTimeDilationTestAccess::Expire(*Time, Parry);
	TestEqual(TEXT("normal after the last step"), Time->GetAppliedScale(), 1.0f);

	// 같은 이름 재요청 = 처음부터(연속 패링).
	Time->PushSequence(Parry, Steps);
	FVBTimeDilationTestAccess::Expire(*Time, Parry);
	Time->PushSequence(Parry, Steps);
	TestEqual(TEXT("re-push restarts at the first step"), Time->GetAppliedScale(), 0.2f);

	// Pop 은 남은 단계까지 지운다 - 늦게 오는 만료가 옛 순서를 되살리지 않는다.
	Time->Pop(Parry);
	TestEqual(TEXT("pop returns to normal"), Time->GetAppliedScale(), 1.0f);
	FVBTimeDilationTestAccess::Expire(*Time, Parry);
	TestEqual(TEXT("a late expiry after pop does not revive the sequence"), Time->GetAppliedScale(), 1.0f);

	// 같은 이름의 단발 요청이 순서를 끊는다.
	Time->PushSequence(Parry, Steps);
	Time->PushTimed(Parry, 0.3f, 0.1f);
	TestEqual(TEXT("single push replaces the step"), Time->GetAppliedScale(), 0.3f);
	FVBTimeDilationTestAccess::Expire(*Time, Parry);
	TestEqual(TEXT("single push ends the sequence"), Time->GetAppliedScale(), 1.0f);

	// 다른 이름의 더 느린 요청(히트스톱)이 이기고, 풀리면 지금 단계로 돌아온다.
	const FName HitStop(TEXT("HitStop"));
	Time->PushSequence(Parry, Steps);
	Time->PushTimed(HitStop, 0.01f, 0.05f);
	TestEqual(TEXT("slower request wins"), Time->GetAppliedScale(), 0.01f);
	FVBTimeDilationTestAccess::Expire(*Time, HitStop);
	TestEqual(TEXT("back to the current step"), Time->GetAppliedScale(), 0.2f);
	Time->Pop(Parry);

	// 길이 있는 단계가 없으면 아무것도 안 건다(경고 하나).
	AddExpectedMessagePlain(TEXT("길이가 있는 단계가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Time->PushSequence(Parry, { Step(0.3f, 0.0f) });
	TestEqual(TEXT("zero-length steps apply nothing"), Time->GetAppliedScale(), 1.0f);
	return true;
}
// 액터 요청(히트스톱 "전투원만"). 액터가 요청 도중 사라져도 늦게 오는 만료가 지운 표를 다시 읽지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTimeDilationActorExpiryTest, "VB.Unit.TimeControl.ActorExpiry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTimeDilationActorExpiryTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UVBTimeDilationSubsystem* Time = World->GetSubsystem<UVBTimeDilationSubsystem>();
	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("time dilation subsystem"), Time) || !TestNotNull(TEXT("actor"), Actor))
	{
		return false;
	}

	const FName Parry(TEXT("Parry"));
	const FName HitStop(TEXT("HitStop"));

	// 가장 느린 요청이 이기고, 풀리는 대로 다음 것, 마지막 뒤에 정상.
	Time->PushActorTimed(Actor, Parry, 0.2f, 0.5f);
	Time->PushActorTimed(Actor, HitStop, 0.01f, 0.05f);
	TestEqual(TEXT("slower actor request wins"), Actor->CustomTimeDilation, 0.01f);
	FVBTimeDilationTestAccess::ExpireActor(*Time, Actor, HitStop);
	TestEqual(TEXT("back to the remaining request"), Actor->CustomTimeDilation, 0.2f);
	FVBTimeDilationTestAccess::ExpireActor(*Time, Actor, Parry);
	TestEqual(TEXT("normal after the last request"), Actor->CustomTimeDilation, 1.0f);
	TestFalse(TEXT("empty request table is dropped"), FVBTimeDilationTestAccess::HasActorRequests(*Time, Actor));

	// 요청 둘이 걸린 채 액터가 사라진다 - 두 만료가 차례로 와도 표를 안전하게 버린다.
	Time->PushActorTimed(Actor, Parry, 0.2f, 0.5f);
	Time->PushActorTimed(Actor, HitStop, 0.01f, 0.05f);
	const TWeakObjectPtr<AActor> Gone(Actor);
	Actor->Destroy();
	FVBTimeDilationTestAccess::ExpireActor(*Time, Gone, HitStop);
	FVBTimeDilationTestAccess::ExpireActor(*Time, Gone, Parry);
	TestFalse(TEXT("destroyed actor leaves no request table"), FVBTimeDilationTestAccess::HasActorRequests(*Time, Gone));
	return true;
}
#endif

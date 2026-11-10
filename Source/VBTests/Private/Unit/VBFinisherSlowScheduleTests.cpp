#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Camera/VBFinisherDirectorSubsystem.h"
#include "Data/VBFinisherConfig.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"

// 처형 슬로 구간은 각자 장면 시간에 한 번씩 켜진다. 여러 구간을 지나는 긴 프레임은 마지막 것을 켜고,
//   Time 0 구간은 시작에 켜져 기존 애셋이 그대로 동작한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherSlowScheduleTest, "VB.Unit.Finisher.SlowSchedule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFinisherSlowScheduleTest::RunTest(const FString& Parameters)
{
	const auto Make = [](float Time)
	{
		FVBFinisherTimeDilationSegment Segment;
		Segment.Time = Time;
		return Segment;
	};
	int32 Next = 0;
	TestEqual(TEXT("No segments: nothing due"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment({}, Next, 5.0f), INDEX_NONE);

	const TArray<FVBFinisherTimeDilationSegment> AtBegin = {Make(0.0f)};
	Next = 0;
	TestEqual(TEXT("Before playback (negative time): nothing due"),
		UVBFinisherDirectorSubsystem::TakeDueSlowSegment(AtBegin, Next, -1.0f), INDEX_NONE);
	TestEqual(TEXT("Time 0 segment fires at begin, as before"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(AtBegin, Next, 0.0f), 0);
	TestEqual(TEXT("It fires once"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(AtBegin, Next, 0.5f), INDEX_NONE);

	const TArray<FVBFinisherTimeDilationSegment> Decisive = {Make(0.4f), Make(0.9f), Make(1.2f)};
	Next = 0;
	TestEqual(TEXT("Not yet at the first segment"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(Decisive, Next, 0.39f), INDEX_NONE);
	TestEqual(TEXT("Index unchanged while waiting"), Next, 0);
	TestEqual(TEXT("Exactly at its time fires it"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(Decisive, Next, 0.4f), 0);
	TestEqual(TEXT("Between segments: nothing new"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(Decisive, Next, 0.8f), INDEX_NONE);
	TestEqual(TEXT("A long frame passing two fires the later one"),
		UVBFinisherDirectorSubsystem::TakeDueSlowSegment(Decisive, Next, 1.3f), 2);
	TestEqual(TEXT("Both passed segments are consumed"), Next, 3);
	TestEqual(TEXT("After the last: nothing"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(Decisive, Next, 9.0f), INDEX_NONE);

	// 늦은 시작(이 기계가 강세 뒤에 시작): 지난 강세는 건너뛰고 아직 창 안이면 켠다.
	FVBFinisherTimeDilationSegment Accent = Make(0.43f);
	Accent.Scale = 0.55f;
	Accent.RealDuration = 0.22f; // window in scene time = 0.43 .. 0.43 + 0.55*0.22 = 0.551
	const TArray<FVBFinisherTimeDilationSegment> F03 = {Accent};
	Next = 0;
	UVBFinisherDirectorSubsystem::SkipExpiredSlowSegments(F03, Next, 0.60f);
	TestEqual(TEXT("Begin after the window: accent skipped"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(F03, Next, 0.60f),
		INDEX_NONE);
	Next = 0;
	UVBFinisherDirectorSubsystem::SkipExpiredSlowSegments(F03, Next, 0.50f);
	TestEqual(TEXT("Begin inside the window: accent still fires"), UVBFinisherDirectorSubsystem::TakeDueSlowSegment(F03, Next, 0.50f), 0);
	Next = 0;
	UVBFinisherDirectorSubsystem::SkipExpiredSlowSegments(F03, Next, 0.05f);
	TestEqual(TEXT("Normal begin: nothing skipped"), Next, 0);

#if WITH_EDITOR
	// 길이 0 구간은 끝없는 Push 가 되어 처형이 끝날 때까지 느려지므로 저장에서 막는다.
	//   여기 설정엔 Scene/database 가 없어 다른 오류도 나므로 이 규칙의 메시지만 찾는다.
	const auto DurationErrors = [](float RealDuration)
	{
		UVBFinisherConfig* Config = NewObject<UVBFinisherConfig>(GetTransientPackage());
		FVBFinisherTimeDilationSegment Segment;
		Segment.Time = 0.43f;
		Segment.RealDuration = RealDuration;
		Config->TimeDilationSegments = {Segment};
		FDataValidationContext Context;
		Config->IsDataValid(Context);
		int32 Count = 0;
		for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
		{
			Count += Issue.Message.ToString().Contains(TEXT("RealDuration")) ? 1 : 0;
		}

		return Count;
	};
	TestEqual(TEXT("RealDuration 0 is rejected at save"), DurationErrors(0.0f), 1);
	TestEqual(TEXT("A positive RealDuration passes this rule"), DurationErrors(0.22f), 0);
#endif
	return true;
}
#endif

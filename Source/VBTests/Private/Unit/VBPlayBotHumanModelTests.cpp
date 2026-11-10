// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotRecorder.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VBPlayBotHumanModelTest
{
	TArray<double> Draw(FVBPlayBotHumanModel& Model, int32 Count, TFunctionRef<double(FVBPlayBotHumanModel&)> Sample)
	{
		TArray<double> Values;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Values.Add(Sample(Model));
		}

		return Values;
	}

	double Quantile(TArray<double> Values, double Q)
	{
		Values.Sort();
		return Values[FMath::Clamp(static_cast<int32>(Q * (Values.Num() - 1)), 0, Values.Num() - 1)];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotHumanModelTest, "VB.Unit.PlayBot.HumanModel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBPlayBotHumanModelTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotHumanModelTest;
	FVBPlayBotHumanProfile Profile;
	FVBPlayBotHumanModel Model;
	Model.Configure(Profile, 1234);

	// 같은 seed, 시도면 같은 사람, 다른 시도면 다른 추첨이다
	Model.BeginTrial(7);
	const TArray<double> First = Draw(Model, 8, [](FVBPlayBotHumanModel& M) { return M.SampleSimpleReactionMs(); });
	Model.BeginTrial(7);
	const TArray<double> Again = Draw(Model, 8, [](FVBPlayBotHumanModel& M) { return M.SampleSimpleReactionMs(); });
	Model.BeginTrial(8);
	const TArray<double> Other = Draw(Model, 8, [](FVBPlayBotHumanModel& M) { return M.SampleSimpleReactionMs(); });
	TestTrue(TEXT("same trial key repeats the same reaction draws"), First == Again);
	TestFalse(TEXT("another trial key draws differently"), First == Other);

	// 역할은 서로 독립이다 - hold 추첨을 더 해도 반응 흐름이 밀리면 안 된다
	Model.BeginTrial(7);
	Draw(Model, 5, [](FVBPlayBotHumanModel& M) { return M.SampleHoldMs(); });
	const TArray<double> AfterHolds = Draw(Model, 8, [](FVBPlayBotHumanModel& M) { return M.SampleSimpleReactionMs(); });
	TestTrue(TEXT("hold draws do not shift reaction draws"), First == AfterHolds);

	// 분포 - 중앙값은 profile 근처, 퍼짐은 실제로 있어야 한다 (고정 지연은 퍼짐 검사에서 실패한다)
	Model.BeginTrial(1);
	const TArray<double> Reactions = Draw(Model, 4000, [](FVBPlayBotHumanModel& M) { return M.SampleSimpleReactionMs(); });
	const double Median = Quantile(Reactions, 0.5);
	TestTrue(FString::Printf(TEXT("reaction median %.1f ms within 5%% of %.1f"), Median, Profile.SimpleMedianMs),
		FMath::Abs(Median - Profile.SimpleMedianMs) <= Profile.SimpleMedianMs * 0.05);
	const double Spread = Quantile(Reactions, 0.9) - Quantile(Reactions, 0.1);
	TestTrue(FString::Printf(TEXT("reaction p10-p90 spread %.1f ms is at least 80 ms (lognormal sigma 0.20 gives ~119)"), Spread),
		Spread >= 80.0);

	const TArray<double> Holds = Draw(Model, 2000, [](FVBPlayBotHumanModel& M) { return M.SampleHoldMs(); });
	TestTrue(TEXT("hold never below HoldMinMs"), Quantile(Holds, 0.0) >= Profile.HoldMinMs);
	TestTrue(TEXT("hold never above HoldMaxMs"), Quantile(Holds, 1.0) <= Profile.HoldMaxMs);

	const TArray<double> Errors = Draw(Model, 4000, [](FVBPlayBotHumanModel& M) { return M.SamplePredictErrorMs(); });
	TestTrue(TEXT("predictive error stays within bias +-3 SD"),
		Quantile(Errors, 0.0) >= Profile.PredictBiasMs - 3.0 * Profile.PredictSdMs && Quantile(Errors,
			1.0) <= Profile.PredictBiasMs + 3.0 * Profile.PredictSdMs);
	TestTrue(TEXT("predictive error is centred on the bias (early presses exist)"),
		FMath::Abs(Quantile(Errors, 0.5) - Profile.PredictBiasMs) <= 5.0);

	const TArray<double> Directions = Draw(Model, 2000, [](FVBPlayBotHumanModel& M) { return M.SampleDirectionErrorDeg(); });
	TestTrue(TEXT("direction error within the clip"),
		Quantile(Directions, 0.0) >= -Profile.MoveDirClipDeg && Quantile(Directions, 1.0) <= Profile.MoveDirClipDeg);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotFrameMarkerTest, "VB.Unit.PlayBot.FrameMarker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBPlayBotFrameMarkerTest::RunTest(const FString& Parameters)
{
	// 분석기가 이 블록에서 프레임 id 를 다시 읽으므로 배치가 곧 계약이다
	constexpr int32 Width = 320;
	constexpr int32 Height = 16;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(40, 40, 40, 255), Width * Height);
	const uint32 Value = 0xA5000F01u;
	FVBPlayBotRecorder::StampMarker(Pixels, Width, Height, Value);

	auto BlockOn = [&Pixels](int32 Block)
	{
		const int32 Center = (FVBPlayBotRecorder::MarkerBlock / 2) * Width + Block * FVBPlayBotRecorder::MarkerBlock + FVBPlayBotRecorder::MarkerBlock / 2;
		return Pixels[Center] == FColor::White;
	};
	TestTrue(TEXT("leading guard block is white"), BlockOn(0));
	TestTrue(TEXT("trailing guard block is white"), BlockOn(FVBPlayBotRecorder::MarkerBits + 1));
	uint32 Decoded = 0;
	for (int32 Bit = 0; Bit < FVBPlayBotRecorder::MarkerBits; ++Bit)
	{
		if (BlockOn(Bit + 1))
		{
			Decoded |= (1u << Bit);
		}
	}

	TestEqual(TEXT("marker decodes to the stamped frame id"), Decoded, Value);
	TestEqual(TEXT("pixels after the marker are untouched"), Pixels[Width - 1], FColor(40, 40, 40, 255));

	// 너무 작은 이미지는 반만 찍지 않고 그대로 둔다
	TArray<FColor> Small;
	Small.Init(FColor(40, 40, 40, 255), 16 * 16);
	FVBPlayBotRecorder::StampMarker(Small, 16, 16, Value);
	TestEqual(TEXT("an image narrower than the marker is not stamped"), Small[0], FColor(40, 40, 40, 255));
	return true;
}

#endif

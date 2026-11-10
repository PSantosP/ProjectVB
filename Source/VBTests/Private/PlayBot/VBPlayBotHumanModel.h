// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

class FConfigFile;

DECLARE_LOG_CATEGORY_EXTERN(LogVBPlayBot, Log, All);

// 합성 플레이어 타이밍 프로필(늘이지 않은 프레임 시계 기준 ms). 값은 Config/DefaultPlayBot.ini 에 있다.
struct FVBPlayBotHumanProfile
{
	FString Name;
	double SimpleMedianMs = 230.0;
	double SimpleSigmaLog = 0.20;
	double PredictBiasMs = -30.0;
	double PredictSdMs = 35.0;
	double HoldMedianMs = 150.0;
	double HoldSigmaLog = 0.35;
	double HoldMinMs = 50.0;
	double HoldMaxMs = 350.0;
	double MoveDirSdDeg = 3.0;
	double MoveDirClipDeg = 9.0;
	double MoveMagnitude = 0.90;
	double CorrectionMinMs = 150.0;
	double CorrectionMaxMs = 300.0;
	double MotorRampMs = 50.0;
	// 상태가 바뀐 뒤 사람이 화면에서 볼 수 있기까지의 프레임 수
	int32 PresentationFrames = 2;

	// 없는 키는 위 기본값을 지키고, 모르는 section 은 오류다
	bool Load(const FConfigFile& Config, const FString& Section, FString& OutError);
	FString ToJson() const;
};

// 결정 역할마다 자기 난수 stream 을 써서, 한 역할의 추가 뽑기가 다른 역할을 밀지 않고
// 같은 번호의 A/B 시행이 같은 사람을 본다.
enum class EVBPlayBotRole : uint8
{
	Reaction,
	Predict,
	Hold,
	Direction,
	Correction,
	Count
};

class FVBPlayBotHumanModel
{
public:
	void Configure(const FVBPlayBotHumanProfile& InProfile, int32 InSeed);
	void BeginTrial(int32 TrialIndex);

	double SampleSimpleReactionMs();
	double SamplePredictErrorMs();
	double SampleHoldMs();
	double SampleDirectionErrorDeg();
	double SampleCorrectionIntervalMs();

	const FVBPlayBotHumanProfile& GetProfile() const { return Profile; }

private:
	double Gaussian(EVBPlayBotRole Role);
	double TruncatedNormal(EVBPlayBotRole Role, double Mean, double Sd, double MinValue, double MaxValue);
	double TruncatedLogNormal(EVBPlayBotRole Role, double Median, double SigmaLog, double MinValue, double MaxValue);

	FVBPlayBotHumanProfile Profile;
	int32 Seed = 0;
	FRandomStream Streams[static_cast<int32>(EVBPlayBotRole::Count)];
};

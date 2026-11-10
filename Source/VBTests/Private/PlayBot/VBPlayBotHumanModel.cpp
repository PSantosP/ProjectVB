// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotHumanModel.h"

#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY(LogVBPlayBot);

namespace
{
	// rejection sampling 으로 경계 안에서 분포 모양을 지킨다. 상한은 깨진 프로필만 막는다
	constexpr int32 MaxRejections = 64;
}

bool FVBPlayBotHumanProfile::Load(const FConfigFile& Config, const FString& Section, FString& OutError)
{
	if (!Config.Contains(Section))
	{
		OutError = FString::Printf(TEXT("profile section [%s] missing"), *Section);
		return false;
	}

	Name = Section;
	const TCHAR* S = *Section;
	Config.GetDouble(S, TEXT("SimpleMedianMs"), SimpleMedianMs);
	Config.GetDouble(S, TEXT("SimpleSigmaLog"), SimpleSigmaLog);
	Config.GetDouble(S, TEXT("PredictBiasMs"), PredictBiasMs);
	Config.GetDouble(S, TEXT("PredictSdMs"), PredictSdMs);
	Config.GetDouble(S, TEXT("HoldMedianMs"), HoldMedianMs);
	Config.GetDouble(S, TEXT("HoldSigmaLog"), HoldSigmaLog);
	Config.GetDouble(S, TEXT("HoldMinMs"), HoldMinMs);
	Config.GetDouble(S, TEXT("HoldMaxMs"), HoldMaxMs);
	Config.GetDouble(S, TEXT("MoveDirSdDeg"), MoveDirSdDeg);
	Config.GetDouble(S, TEXT("MoveDirClipDeg"), MoveDirClipDeg);
	Config.GetDouble(S, TEXT("MoveMagnitude"), MoveMagnitude);
	Config.GetDouble(S, TEXT("CorrectionMinMs"), CorrectionMinMs);
	Config.GetDouble(S, TEXT("CorrectionMaxMs"), CorrectionMaxMs);
	Config.GetDouble(S, TEXT("MotorRampMs"), MotorRampMs);
	Config.GetInt(S, TEXT("PresentationFrames"), PresentationFrames);
	if (SimpleMedianMs <= 0.0 || HoldMedianMs <= 0.0 || HoldMinMs > HoldMaxMs || CorrectionMinMs > CorrectionMaxMs
		|| PredictSdMs < 0.0 || MoveMagnitude <= 0.0 || MoveMagnitude > 1.0 || PresentationFrames < 0)
	{
		OutError = FString::Printf(TEXT("profile [%s] has an invalid value"), *Section);
		return false;
	}

	return true;
}

FString FVBPlayBotHumanProfile::ToJson() const
{
	return FString::Printf(
		TEXT("{\"name\":\"%s\",\"simpleMedianMs\":%.3f,\"simpleSigmaLog\":%.4f,\"predictBiasMs\":%.3f,\"predictSdMs\":%.3f,")
		TEXT("\"holdMedianMs\":%.3f,\"holdSigmaLog\":%.4f,\"holdMinMs\":%.3f,\"holdMaxMs\":%.3f,\"moveDirSdDeg\":%.3f,")
		TEXT("\"moveDirClipDeg\":%.3f,\"moveMagnitude\":%.3f,\"correctionMinMs\":%.3f,\"correctionMaxMs\":%.3f,")
		TEXT("\"motorRampMs\":%.3f,\"presentationFrames\":%d}"),
		*Name, SimpleMedianMs, SimpleSigmaLog, PredictBiasMs, PredictSdMs, HoldMedianMs, HoldSigmaLog, HoldMinMs, HoldMaxMs,
		MoveDirSdDeg, MoveDirClipDeg, MoveMagnitude, CorrectionMinMs, CorrectionMaxMs, MotorRampMs, PresentationFrames);
}

void FVBPlayBotHumanModel::Configure(const FVBPlayBotHumanProfile& InProfile, int32 InSeed)
{
	Profile = InProfile;
	Seed = InSeed;
	BeginTrial(0);
}

void FVBPlayBotHumanModel::BeginTrial(int32 TrialIndex)
{
	for (int32 Role = 0; Role < static_cast<int32>(EVBPlayBotRole::Count); ++Role)
	{
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(Seed), GetTypeHash(TrialIndex)), GetTypeHash(Role));
		Streams[Role].Initialize(static_cast<int32>(Key));
	}
}

double FVBPlayBotHumanModel::Gaussian(EVBPlayBotRole Role)
{
	FRandomStream& Stream = Streams[static_cast<int32>(Role)];
	// Box-Muller - (0,1] 범위라 log 가 유한하다
	const double U1 = FMath::Max(static_cast<double>(Stream.GetFraction()), UE_DOUBLE_SMALL_NUMBER);
	const double U2 = Stream.GetFraction();
	return FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(UE_DOUBLE_TWO_PI * U2);
}

double FVBPlayBotHumanModel::TruncatedNormal(EVBPlayBotRole Role, double Mean, double Sd, double MinValue, double MaxValue)
{
	for (int32 Try = 0; Try < MaxRejections; ++Try)
	{
		const double Value = Mean + Sd * Gaussian(Role);
		if (Value >= MinValue && Value <= MaxValue)
		{
			return Value;
		}
	}

	return FMath::Clamp(Mean, MinValue, MaxValue);
}

double FVBPlayBotHumanModel::TruncatedLogNormal(EVBPlayBotRole Role, double Median, double SigmaLog, double MinValue, double MaxValue)
{
	for (int32 Try = 0; Try < MaxRejections; ++Try)
	{
		const double Value = FMath::Exp(FMath::Loge(Median) + SigmaLog * Gaussian(Role));
		if (Value >= MinValue && Value <= MaxValue)
		{
			return Value;
		}
	}

	return FMath::Clamp(Median, MinValue, MaxValue);
}

double FVBPlayBotHumanModel::SampleSimpleReactionMs()
{
	return TruncatedLogNormal(EVBPlayBotRole::Reaction, Profile.SimpleMedianMs, Profile.SimpleSigmaLog, 0.0, TNumericLimits<double>::Max());
}

double FVBPlayBotHumanModel::SamplePredictErrorMs()
{
	const double Bound = 3.0 * Profile.PredictSdMs;
	return TruncatedNormal(EVBPlayBotRole::Predict, Profile.PredictBiasMs, Profile.PredictSdMs,
		Profile.PredictBiasMs - Bound, Profile.PredictBiasMs + Bound);
}

double FVBPlayBotHumanModel::SampleHoldMs()
{
	return TruncatedLogNormal(EVBPlayBotRole::Hold, Profile.HoldMedianMs, Profile.HoldSigmaLog, Profile.HoldMinMs, Profile.HoldMaxMs);
}

double FVBPlayBotHumanModel::SampleDirectionErrorDeg()
{
	return TruncatedNormal(EVBPlayBotRole::Direction, 0.0, Profile.MoveDirSdDeg, -Profile.MoveDirClipDeg, Profile.MoveDirClipDeg);
}

double FVBPlayBotHumanModel::SampleCorrectionIntervalMs()
{
	FRandomStream& Stream = Streams[static_cast<int32>(EVBPlayBotRole::Correction)];
	return FMath::Lerp(Profile.CorrectionMinMs, Profile.CorrectionMaxMs, static_cast<double>(Stream.GetFraction()));
}

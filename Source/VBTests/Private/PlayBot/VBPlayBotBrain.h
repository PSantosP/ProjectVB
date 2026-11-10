// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlayBot/VBPlayBotInputDriver.h"

class FVBPlayBotHumanModel;
// FVBPlayBotComboParams - 콤보 정책이 쓰는 시나리오 수치 (FighterLightStop ini 구역에서 읽는다)
struct FVBPlayBotComboParams
{
	FName AttackAction;
	double AttackRange = 150.0;
	int32 PracticeStopAfter = 4;
	double TrialTimeoutSeconds = 8.0;
	double ContactTimeoutSeconds = 1.5;
	double HoldAwaySeconds = 2.0;
	// 켜야 쓰는 진단용 값 - -1 이면 사람 예측 정책을 그대로 쓰고, 연습은 늘 그대로 쓴다
	int32 AwayAfterWindowFrames = -1;
};

// FVBPlayBotPercept - 사람이 이 프레임에 알 수 있는 것만 담는다 (콤보 창, ability tag 같은 정답은 일부러 뺀다)
struct FVBPlayBotPercept
{
	bool bValid = false;
	double Distance = 0.0;
	// 조작 기준 대상 방향 - X = 오른쪽, Y = 앞 (스틱 축)
	FVector2D ToTargetStick = FVector2D::ZeroVector;
	double Speed = 0.0;
};

struct FVBPlayBotTrialPlan
{
	int32 Index = 0;
	int32 PairKey = 0;
	FString Variant;
	int32 StopAfter = 1;
	bool bPractice = false;
};

// FVBPlayBotBrain - 다가가서 StopAfter 타까지 경공격하고, 스틱을 반대로 밀어 버틴다
// 연습 시도는 본 접촉에 반응하며 타마다 누름->접촉 간격을 배우고, 측정 시도는 앞 입력이 나갈 때 만든 예측으로 누른다
class FVBPlayBotBrain
{
public:
	TFunction<void(const FString& Json)> Emit;

	void Configure(FVBPlayBotHumanModel& InHuman, FVBPlayBotInputDriver& InInput, const FVBPlayBotComboParams& InParams);
	void BeginTrial(const FVBPlayBotTrialPlan& Plan, int64 Frame);

	// 접촉 = 콤보 Index 타(1부터)의 첫 피격, 게임이 CueFrame 에 알린 그대로
	void OnContact(int32 Index, int64 CueFrame);
	void OnPressInjected(const FVBPlayBotPress& Press);
	// 창 정답은 진단용에서만 주고, 사람의 지각이 아니다
	void OnDiagnosticWindow(bool bOpen, int32 HitIndex, int64 Frame);

	// 시도가 끝나면 true 이고 이유는 EndReason 에 있다
	bool Tick(int64 Frame, const FVBPlayBotPercept& Percept);
	const FString& GetEndReason() const { return EndReason; }

	bool FinalizePracticeModel(FString& OutError);
	// 배운 타이밍이 있는 가장 높은 타 - 그 뒤 타가 필요한 측정 시도는 건너뛰고 기록한다
	int32 GetMaxModeledHit() const { return MaxModeledHit; }
	FString PracticeModelJson() const;

private:
	enum class EPhase : uint8 { Approach, Combo, StopHold, Done };

	void SchedulePress(int32 HitIndex, const FString& Cue, int64 CueFrame, int64 ObservedFrame,
		int64 PredictionFrame, int64 PredictedCueFrame, double DelayMs, double ErrorMs, int64 DueFrame);
	void ScheduleStop(const FString& Cue, int64 CueFrame, int64 ObservedFrame, int64 PredictionFrame,
		int64 PredictedCueFrame, double DelayMs, double ErrorMs, int64 DueFrame);
	void UpdateApproachStick(int64 Frame, const FVBPlayBotPercept& Percept);
	void Finish(const FString& Reason);
	int64 MsToFrames(double Ms) const;

	FVBPlayBotHumanModel* Human = nullptr;
	FVBPlayBotInputDriver* Input = nullptr;
	FVBPlayBotComboParams Scenario;

	FVBPlayBotTrialPlan Plan;
	EPhase Phase = EPhase::Done;
	int64 TrialStartFrame = 0;
	int64 NextCorrectionFrame = 0;
	bool bInRangeSeen = false;
	int32 PressesScheduled = 0;
	int32 PressesInjected = 0;
	TMap<int32, int64> InjectedFrame;
	TMap<int32, int64> ContactFrame;
	int64 StopDueFrame = -1;
	int64 StopStartFrame = -1;
	FVector2D AwayStick = FVector2D::ZeroVector;
	FString StopRecord;
	FString EndReason;

	// 연습에서 타마다 잰 누름->접촉 프레임 - 모델은 타별 중앙값이다
	TMap<int32, TArray<int64>> PracticeSamples;
	TMap<int32, int64> Model;
	int32 MaxModeledHit = 0;
};

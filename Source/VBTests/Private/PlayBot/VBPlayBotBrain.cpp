// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotBrain.h"

#include "PlayBot/VBPlayBotFrame.h"
#include "PlayBot/VBPlayBotHumanModel.h"

void FVBPlayBotBrain::Configure(FVBPlayBotHumanModel& InHuman, FVBPlayBotInputDriver& InInput, const FVBPlayBotComboParams& InParams)
{
	Human = &InHuman;
	Input = &InInput;
	Scenario = InParams;
}

int64 FVBPlayBotBrain::MsToFrames(double Ms) const
{
	return VBPlayBot::MsToFrames(Ms);
}

void FVBPlayBotBrain::BeginTrial(const FVBPlayBotTrialPlan& InPlan, int64 Frame)
{
	Plan = InPlan;
	Phase = EPhase::Approach;
	TrialStartFrame = Frame;
	NextCorrectionFrame = Frame;
	bInRangeSeen = false;
	PressesScheduled = 0;
	PressesInjected = 0;
	InjectedFrame.Reset();
	ContactFrame.Reset();
	StopDueFrame = -1;
	StopStartFrame = -1;
	AwayStick = FVector2D::ZeroVector;
	StopRecord.Reset();
	EndReason.Reset();
}

void FVBPlayBotBrain::SchedulePress(int32 HitIndex, const FString& Cue, int64 CueFrame, int64 ObservedFrame,
	int64 PredictionFrame, int64 PredictedCueFrame, double DelayMs, double ErrorMs, int64 DueFrame)
{
	FVBPlayBotPress Press;
	Press.Action = Scenario.AttackAction;
	Press.Cue = FString::Printf(TEXT("%s#%d"), *Cue, HitIndex);
	Press.CueFrame = CueFrame;
	Press.ObservedFrame = ObservedFrame;
	Press.PredictionFrame = PredictionFrame;
	Press.PredictedCueFrame = PredictedCueFrame;
	Press.DelayMs = DelayMs;
	Press.ErrorMs = ErrorMs;
	Press.DueFrame = DueFrame;
	Press.HoldFrames = FMath::Max<int32>(1, static_cast<int32>(MsToFrames(Human->SampleHoldMs())));
	Input->Schedule(MoveTemp(Press));
	++PressesScheduled;
}

void FVBPlayBotBrain::ScheduleStop(const FString& Cue, int64 CueFrame, int64 ObservedFrame, int64 PredictionFrame,
	int64 PredictedCueFrame, double DelayMs, double ErrorMs, int64 DueFrame)
{
	StopDueFrame = DueFrame;
	StopRecord = FString::Printf(
		TEXT("\"cue\":\"%s\",\"cue_f\":%lld,\"obs_f\":%lld,\"pred_f\":%lld,\"pred_cue_f\":%lld,\"delay_ms\":%.3f,\"err_ms\":%.3f,\"due_f\":%lld"),
		*Cue, CueFrame, ObservedFrame, PredictionFrame, PredictedCueFrame, DelayMs, ErrorMs, DueFrame);
}

void FVBPlayBotBrain::OnContact(int32 Index, int64 CueFrame)
{
	if (Phase != EPhase::Combo || ContactFrame.Contains(Index))
	{
		return;
	}

	ContactFrame.Add(Index, CueFrame);
	if (const int64* Injected = InjectedFrame.Find(Index))
	{
		if (Plan.bPractice)
		{
			PracticeSamples.FindOrAdd(Index).Add(CueFrame - *Injected);
		}
	}

	if (!Plan.bPractice)
	{
		return;
	}

	// 연습은 본 것에 반응한다 - 표시 지연 뒤 화면의 접촉, 그다음 단순 반응
	const int64 Observed = CueFrame + Human->GetProfile().PresentationFrames;
	const double DelayMs = Human->SampleSimpleReactionMs();
	const int64 Due = Observed + MsToFrames(DelayMs);
	if (Index < Plan.StopAfter && PressesScheduled == Index)
	{
		SchedulePress(Index + 1, TEXT("seen_contact"), CueFrame, Observed, -1, -1, DelayMs, 0.0, Due);
	}
	else if (Index == Plan.StopAfter && StopDueFrame < 0)
	{
		ScheduleStop(FString::Printf(TEXT("seen_contact#%d"), Index), CueFrame, Observed, -1, -1, DelayMs, 0.0, Due);
	}
}

void FVBPlayBotBrain::OnPressInjected(const FVBPlayBotPress& Press)
{
	if (Phase != EPhase::Approach && Phase != EPhase::Combo)
	{
		return;
	}

	++PressesInjected;
	InjectedFrame.Add(PressesInjected, Press.InjectedFrame);
	Phase = EPhase::Combo;
	// 공격을 누르면 엄지가 스틱에서 떨어진다
	Input->SetStickWant(FVector2D::ZeroVector);
	if (Plan.bPractice)
	{
		return;
	}

	const int64* Learned = Model.Find(PressesInjected);
	if (!Learned)
	{
		return;
	}
	// 예측은 그 접촉이 오기 전, 지금 만든다
	const int64 PredictedContact = Press.InjectedFrame + *Learned;
	const double ErrorMs = Human->SamplePredictErrorMs();
	const int64 Due = PredictedContact + static_cast<int64>(FMath::RoundToDouble(ErrorMs / VBPlayBot::FrameMs));
	if (PressesInjected < Plan.StopAfter)
	{
		SchedulePress(PressesInjected + 1, TEXT("predicted_contact"), -1, -1, Press.InjectedFrame, PredictedContact, 0.0, ErrorMs, Due);
	}
	else if (PressesInjected == Plan.StopAfter && Scenario.AwayAfterWindowFrames < 0)
	{
		ScheduleStop(FString::Printf(TEXT("predicted_contact#%d"), PressesInjected), -1, -1, Press.InjectedFrame, PredictedContact,
			0.0, ErrorMs, Due);
	}
}

void FVBPlayBotBrain::OnDiagnosticWindow(bool bOpen, int32 HitIndex, int64 Frame)
{
	if (Scenario.AwayAfterWindowFrames < 0 || Plan.bPractice || Phase != EPhase::Combo
		|| PressesInjected != Plan.StopAfter)
	{
		return;
	}

	if (!bOpen)
	{
		if (StopDueFrame < 0)
		{
			return;
		}
		// 놓쳤거나 너무 짧은 창은 새로 밀기 시도를 마친 것이 아니다
		Finish(TEXT("repush_window_closed"));
		return;
	}

	if (HitIndex != Plan.StopAfter || StopDueFrame >= 0)
	{
		return;
	}

	Input->SetStickWant(FVector2D::ZeroVector);
	// 공격 입력이 이미 접근 스틱을 놓았다 - 창에서도 중립이 관찰돼야 한다
	if (Input->GetStickOut().Size() > 0.1)
	{
		Finish(TEXT("repush_not_neutral"));
		return;
	}

	ScheduleStop(FString::Printf(TEXT("combo_window#%d"), HitIndex), Frame, Frame, -1, -1,
		Scenario.AwayAfterWindowFrames * VBPlayBot::FrameMs, 0.0, Frame + Scenario.AwayAfterWindowFrames);
}

void FVBPlayBotBrain::UpdateApproachStick(int64 Frame, const FVBPlayBotPercept& Percept)
{
	if (Frame < NextCorrectionFrame)
	{
		return;
	}
	// 사람은 매 프레임이 아니라 간격을 두고, 완벽하지 않게 다시 겨눈다
	const double ErrorDeg = Human->SampleDirectionErrorDeg();
	const FVector2D Aim = Percept.ToTargetStick.GetRotated(ErrorDeg) * Human->GetProfile().MoveMagnitude;
	Input->SetStickWant(Aim);
	NextCorrectionFrame = Frame + FMath::Max<int64>(1, MsToFrames(Human->SampleCorrectionIntervalMs()));
	if (Emit)
	{
		Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"aim\",\"f\":%lld,\"want\":[%.4f,%.4f],\"err_deg\":%.3f}"), Frame, Aim.X, Aim.Y, ErrorDeg));
	}
}

bool FVBPlayBotBrain::Tick(int64 Frame, const FVBPlayBotPercept& Percept)
{
	if (Phase == EPhase::Done)
	{
		return true;
	}

	if (!Percept.bValid)
	{
		Finish(TEXT("lost_target"));
		return true;
	}

	if (Frame - TrialStartFrame > MsToFrames(Scenario.TrialTimeoutSeconds * 1000.0))
	{
		Finish(TEXT("timeout"));
		return true;
	}

	switch (Phase)
	{
	case EPhase::Approach:
		UpdateApproachStick(Frame, Percept);
		if (!bInRangeSeen && Percept.Distance <= Scenario.AttackRange)
		{
			bInRangeSeen = true;
			const int64 Observed = Frame + Human->GetProfile().PresentationFrames;
			const double DelayMs = Human->SampleSimpleReactionMs();
			SchedulePress(1, TEXT("in_range"), Frame, Observed, -1, -1, DelayMs, 0.0, Observed + MsToFrames(DelayMs));
		}
		break;

	case EPhase::Combo:
	{
		// 제때 접촉을 못 낸 입력은 연타를 끝낸다 - 실패를 다시 시도하지 않고 남긴다
		const int64* LastInjected = InjectedFrame.Find(PressesInjected);
		if (LastInjected && !ContactFrame.Contains(PressesInjected)
			&& Frame - *LastInjected > MsToFrames(Scenario.ContactTimeoutSeconds * 1000.0))
		{
			Finish(FString::Printf(TEXT("no_contact#%d"), PressesInjected));
			return true;
		}

		if (StopDueFrame >= 0 && Frame >= StopDueFrame)
		{
			Input->CancelPending(TEXT("stop_intent"));
			AwayStick = (-Percept.ToTargetStick).GetSafeNormal() * Human->GetProfile().MoveMagnitude;
			Input->SetStickWant(AwayStick);
			StopStartFrame = Frame;
			Phase = EPhase::StopHold;
			if (Emit)
			{
				Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"stop\",\"f\":%lld,%s,\"inj_f\":%lld,\"late\":%s,\"want\":[%.4f,%.4f]}"),
					Frame, *StopRecord, Frame, Frame > StopDueFrame ? TEXT("true") : TEXT("false"), AwayStick.X, AwayStick.Y));
			}
		}
		break;
	}

	case EPhase::StopHold:
		if (Frame - StopStartFrame >= MsToFrames(Scenario.HoldAwaySeconds * 1000.0))
		{
			Finish(TEXT("completed"));
			return true;
		}
		break;

	default:
		break;
	}

	return false;
}

void FVBPlayBotBrain::Finish(const FString& Reason)
{
	Phase = EPhase::Done;
	EndReason = Reason;
}

bool FVBPlayBotBrain::FinalizePracticeModel(FString& OutError)
{
	Model.Reset();
	MaxModeledHit = 0;
	// 모델은 연습에서 맞은 1..N 타만 다룬다 - 한 번도 안 맞은 타는 사람이 배울 타이밍이 없다
	for (int32 Hit = 1; Hit <= Scenario.PracticeStopAfter; ++Hit)
	{
		TArray<int64> Samples = PracticeSamples.FindRef(Hit);
		if (Samples.IsEmpty())
		{
			break;
		}

		Samples.Sort();
		Model.Add(Hit, Samples[Samples.Num() / 2]);
		MaxModeledHit = Hit;
	}

	if (MaxModeledHit == 0)
	{
		OutError = TEXT("practice produced no press->contact sample for hit 1");
		return false;
	}

	return true;
}

FString FVBPlayBotBrain::PracticeModelJson() const
{
	TArray<FString> Parts;
	for (const TPair<int32, int64>& Pair : Model)
	{
		TArray<FString> Samples;
		for (const int64 Sample : PracticeSamples.FindRef(Pair.Key))
		{
			Samples.Add(FString::Printf(TEXT("%lld"), Sample));
		}

		Parts.Add(FString::Printf(TEXT("\"%d\":{\"median_f\":%lld,\"samples_f\":[%s]}"), Pair.Key, Pair.Value, *FString::Join(Samples, TEXT(","))));
	}

	return FString::Printf(TEXT("{\"maxModeledHit\":%d,%s}"), MaxModeledHit, *FString::Join(Parts, TEXT(",")));
}

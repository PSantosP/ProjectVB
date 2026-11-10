// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

// 오디오 캡처 탐침 - 전투 없이 정해진 프레임에 소리를 틀어, audio.wav 에 종류별로 얼마나 늦게 찍히는지(캡처 자체 지연) 잰다
// 타격, 소리 동기 검사의 기준값이다. probe = "Name|SoundPath|AttenuationPath"(attenuation 비우면 소리 자체 값)
// Repeats 번, GapSeconds 간격, 플레이어 앞 ForwardCm 에서 틀고, 매번 프레임과 함께 {"e":"probe"} 를 낸다
class FVBPlayBotScenario_AudioProbe : public FVBPlayBotScenarioBase
{
	// 시나리오는 UObject 가 아니라 GC 너머로 asset 을 붙잡지 못하므로 포인터 대신 경로를 둔다
	// Setup 이 모든 경로가 로드되는지 확인하고, Play 는 다시 로드한다(이미 올라와 있어 싸다)
	struct FProbe
	{
		FString Name;
		FString SoundPath;
		FString AttenuationPath;
	};

public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		const double Settle = C.GetDouble(TEXT("SettleSeconds"), 2.0);
		GapSeconds = C.GetDouble(TEXT("GapSeconds"), 1.5);
		ForwardCm = C.GetDouble(TEXT("ForwardCm"), 150.0);
		const int32 Repeats = C.GetInt(TEXT("Repeats"), 3);
		if (Settle < 0 || GapSeconds <= 0 || Repeats <= 0)
		{
			OutError = FString::Printf(TEXT("[%s] invalid SettleSeconds/GapSeconds/Repeats"), *C.Section);
			return false;
		}

		for (const FString& Line : C.GetArray(TEXT("Probe")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT("|"), /*InCullEmpty=*/false);
			FProbe Probe;
			Probe.Name = Parts.IsValidIndex(0) ? Parts[0] : FString();
			Probe.SoundPath = Parts.IsValidIndex(1) ? Parts[1] : FString();
			Probe.AttenuationPath = Parts.IsValidIndex(2) ? Parts[2] : FString();
			if (!Probe.AttenuationPath.IsEmpty() && !LoadObject<USoundAttenuation>(nullptr, *Probe.AttenuationPath))
			{
				OutError = FString::Printf(TEXT("[%s] probe '%s': attenuation did not load: %s"), *C.Section, *Probe.Name,
					*Probe.AttenuationPath);
				return false;
			}

			if (Probe.Name.IsEmpty() || Probe.SoundPath.IsEmpty() || !LoadObject<USoundBase>(nullptr, *Probe.SoundPath))
			{
				OutError = FString::Printf(TEXT("[%s] probe line needs Name|SoundPath[|AttenuationPath] with a loadable sound: %s"), *C.Section, *Line);
				return false;
			}

			Probes.Add(Probe);
		}

		if (Probes.IsEmpty())
		{
			OutError = FString::Printf(TEXT("[%s] no +Probe lines"), *C.Section);
			return false;
		}

		Sequence.Then<FVBSkillWait>(TEXT("settle"), Settle);
		for (int32 Repeat = 0; Repeat < Repeats; ++Repeat)
		{
			for (int32 Index = 0; Index < Probes.Num(); ++Index)
			{
				Sequence.Then<FVBSkillDo>(FString::Printf(TEXT("play %s %d"), *Probes[Index].Name, Repeat + 1),
					[this, Index](FVBPlayBotContext& C2, FString& Error)
				{
					return Play(C2, Probes[Index], Error);
				});
				Sequence.Then<FVBSkillWait>(TEXT("gap"), GapSeconds);
			}
		}

		Sequence.Then<FVBSkillDo>(TEXT("summary"), [this](FVBPlayBotContext& C2, FString&)
		{
			C2.Checks->Look(TEXT("audio probes"), FirstPlayFrame, C2.Frame,
				FString::Printf(TEXT("%d plays - judge onsets in audio.wav against the probe events"), Plays));
			return true;
		});
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		return State == EVBPlayBotState::Failed ? Fail(Sequence.GetFailure()) : State;
	}

private:
	bool Play(FVBPlayBotContext& Ctx, const FProbe& Probe, FString& Error)
	{
		if (!Ctx.Player || !Ctx.World)
		{
			Error = TEXT("no player or world to place the probe");
			return false;
		}

		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Probe.SoundPath);
		USoundAttenuation* Attenuation = Probe.AttenuationPath.IsEmpty() ? nullptr : LoadObject<USoundAttenuation>(nullptr,
			*Probe.AttenuationPath);
		if (!Sound || (!Probe.AttenuationPath.IsEmpty() && !Attenuation))
		{
			Error = FString::Printf(TEXT("probe '%s' no longer loads"), *Probe.Name);
			return false;
		}

		const FVector Location = Ctx.Player->GetActorLocation() + Ctx.Player->GetActorForwardVector().GetSafeNormal2D() * ForwardCm;
		UGameplayStatics::PlaySoundAtLocation(Ctx.World, Sound, Location, FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, Attenuation);
		if (FirstPlayFrame < 0)
		{
			FirstPlayFrame = Ctx.Frame;
		}

		++Plays;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"probe\",\"f\":%lld,\"name\":\"%s\",\"sound\":\"%s\",\"attenuation\":\"%s\"}"),
			Ctx.Frame, *Probe.Name, *Sound->GetName(), *GetNameSafe(Attenuation)));
		return true;
	}

	FVBPlayBotSequence Sequence;
	TArray<FProbe> Probes;
	double GapSeconds = 1.5;
	double ForwardCm = 150.0;
	int64 FirstPlayFrame = -1;
	int32 Plays = 0;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_AudioProbe, "AudioProbe")

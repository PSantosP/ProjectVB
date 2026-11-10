// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBOptionsSubsystem.h"

#include "AudioDeviceHandle.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Engine/UserInterfaceSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"
#include "CoreLoop/VBDifficultyRules.h"
#include "CoreLoop/VBDifficultySettings.h"
#include "CoreLoop/VBDifficultyStateComponent.h"
#include "Settings/VBCulture.h"
#include "Settings/VBOptionRules.h"
#include "Settings/VBOptionsSettings.h"
#include "Settings/VBUserSettingsStore.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/Package.h"
#include "VB/VB.h"

namespace
{
	bool CommitToEngine(const FVBMachineSettings& Draft)
	{
		UGameUserSettings* Machine = UGameUserSettings::GetGameUserSettings();
		if (!Machine)
		{
			return false;
		}

		// 화면 모드 말고는 바로 걸고 저장한다. 화면 모드는 지금 값으로 저장한 뒤 미리 보기로만 바꾼다(ApplySettings 는 저장까지 한다).
		const FVBMachineCommitPlan Plan = Draft.PlanCommit(FVBMachineSettings::Read(*Machine));
		Machine->ConfirmVideoMode();
		Plan.Saved.Write(*Machine);
		Machine->ApplyNonResolutionSettings();
		// 업스케일링 자동(0)은 엔진이 아무것도 안 걸어 이전 배율이 남는다 - 같은 우선순위로 직접 되돌린다.
		if (Plan.bResetScreenPercentage)
		{
			if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
			{
				ScreenPercentage->Set(0.0f, ECVF_SetByScalability);
			}
		}

		Machine->SaveSettings();
		if (!Plan.bPreviewVideoMode)
		{
			return false;
		}

		Machine->SetScreenResolution(Draft.Resolution);
		Machine->SetFullscreenMode(Draft.WindowMode);
		Machine->ApplyResolutionSettings(false);
		return true;
	}

	void DecideVideoModeInEngine(bool bKeep)
	{
		UGameUserSettings* Machine = UGameUserSettings::GetGameUserSettings();
		if (!Machine)
		{
			return;
		}

		if (bKeep)
		{
			Machine->ConfirmVideoMode();
		}
		else
		{
			Machine->RevertVideoMode();
			Machine->ApplyResolutionSettings(false);
		}

		Machine->SaveSettings();
	}

	void RunAutoDetectInEngine()
	{
		if (UGameUserSettings* Machine = UGameUserSettings::GetGameUserSettings())
		{
			Machine->RunHardwareBenchmark();
			Machine->ApplyHardwareBenchmarkResults();
		}
	}
}

FVBOptionsMachineActions FVBOptionsMachineActions::Engine()
{
	FVBOptionsMachineActions Actions;
	Actions.Commit = &CommitToEngine;
	Actions.DecideVideoMode = &DecideVideoModeInEngine;
	Actions.RunAutoDetect = &RunAutoDetectInEngine;
	return Actions;
}

UVBOptionsSubsystem* UVBOptionsSubsystem::Get(const ULocalPlayer* LocalPlayer)
{
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVBOptionsSubsystem>() : nullptr;
}

FVBOptionsDraft UVBOptionsSubsystem::MakeDraft() const
{
	FVBOptionsDraft Draft;
	Draft.User = UVBUserSettingsStore::GetSettingsData(GetLocalPlayer());
	if (const UGameUserSettings* Machine = UGameUserSettings::GetGameUserSettings())
	{
		Draft.Machine = FVBMachineSettings::Read(*Machine);
	}

	Draft.Culture = VBCulture::GetActive();
	// 게임 월드의 난이도 상태 컴포넌트가 있을 때만(복제 값 - 원격 클라도 지금 단계를 본다). 메인 메뉴는 빈 태그.
	if (const UVBDifficultyStateComponent* Difficulty = UVBDifficultyStateComponent::Get(GetLocalPlayer()))
	{
		Draft.Difficulty = Difficulty->GetLevel();
	}

	return Draft;
}

FVBOptionsDraft UVBOptionsSubsystem::MakeDefaultDraft() const
{
	FVBOptionsDraft Defaults;
	if (const UGameUserSettings* Current = UGameUserSettings::GetGameUserSettings())
	{
		UGameUserSettings* Fresh = NewObject<UGameUserSettings>(GetTransientPackage(), Current->GetClass());
		Fresh->SetToDefaults();
		Defaults.Machine = FVBMachineSettings::Read(*Fresh);
		Defaults.Machine.Resolution = Current->GetDesktopResolution();
	}

	Defaults.Culture = VBCulture::GetDefault();
	// 기본값 되돌리기는 설정 자산의 기본 단계. 원격 클라는 바꿀 수 없으니 지금 단계 그대로 둔다(서버가 늘 거절하는 초안 방지).
	if (const UVBDifficultyStateComponent* Difficulty = UVBDifficultyStateComponent::Get(GetLocalPlayer()))
	{
		const bool bRemoteClient = GetWorld() && GetWorld()->GetNetMode() == NM_Client;
		Defaults.Difficulty = bRemoteClient ? Difficulty->GetLevel()
			: VBDifficultyRules::NormalizeLevel(UVBDifficultySettings::LoadConfig(), FGameplayTag());
	}

	return Defaults;
}

bool UVBOptionsSubsystem::Commit(const FVBOptionsDraft& Draft)
{
	const FVBSettingsSaveData User = VBOptionRules::Sanitize(Draft.User, *GetDefault<UVBOptionsSettings>());
	if (UVBUserSettingsStore* Store = UVBUserSettingsStore::Get(GetLocalPlayer()))
	{
		Store->UpdateSettings(User);
	}

	ApplyUserSettings(User);
	OnUserSettingsChanged.Broadcast(User);

	if (!Draft.Culture.IsEmpty() && Draft.Culture != VBCulture::GetActive())
	{
		VBCulture::Apply(Draft.Culture);
	}

	// 난이도는 서버에 요청만 한다. 적용 확인은 화면이 복제된 단계를 다시 읽어서 한다.
	const UVBDifficultyStateComponent* Difficulty = UVBDifficultyStateComponent::Get(GetLocalPlayer());
	if (Difficulty && Draft.Difficulty.IsValid() && Draft.Difficulty != Difficulty->GetLevel())
	{
		const bool bSent = DifficultyRequest.ExecuteIfBound(Draft.Difficulty);
		VB_CLOG(!bSent, Warning, "옵션: 난이도 %s 요청을 보낼 PC 가 없다 - 바뀌지 않는다", *Draft.Difficulty.ToString());
	}

	RevertVideoMode();
	if (!MachineActions.Commit(Draft.Machine))
	{
		return false;
	}

	PendingVideoDeadline = FPlatformTime::Seconds() + GetDefault<UVBOptionsSettings>()->VideoConfirmSeconds;
	PendingVideoTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UVBOptionsSubsystem::TickPendingVideo));
	return true;
}

void UVBOptionsSubsystem::KeepVideoMode()
{
	FinishVideoPreview(/*bKeep=*/true);
}

void UVBOptionsSubsystem::RevertVideoMode()
{
	FinishVideoPreview(/*bKeep=*/false);
}

void UVBOptionsSubsystem::FinishVideoPreview(bool bKeep)
{
	if (HasPendingVideoMode())
	{
		ClearPendingVideo();
		MachineActions.DecideVideoMode(bKeep);
	}
}

double UVBOptionsSubsystem::GetPendingVideoSecondsLeft() const
{
	return HasPendingVideoMode() ? FMath::Max(0.0, PendingVideoDeadline - FPlatformTime::Seconds()) : 0.0;
}

bool UVBOptionsSubsystem::TickPendingVideo(float DeltaTime)
{
	// 실시간으로 잰다 - 일시정지 메뉴에서 열면 월드 시간이 멈춰 있다.
	if (FPlatformTime::Seconds() < PendingVideoDeadline)
	{
		return true;
	}

	PendingVideoTicker.Reset(); // 이 틱이 끝나면 엔진이 지운다(false 반환)
	RevertVideoMode();
	return false;
}

void UVBOptionsSubsystem::ClearPendingVideo()
{
	PendingVideoDeadline = 0.0;
	if (PendingVideoTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PendingVideoTicker);
		PendingVideoTicker.Reset();
	}
}

void UVBOptionsSubsystem::RunAutoDetect()
{
	MachineActions.RunAutoDetect();
}

TArray<FIntPoint> UVBOptionsSubsystem::GetSelectableResolutions() const
{
	TArray<FIntPoint> Resolutions;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	if (const UGameUserSettings* Machine = UGameUserSettings::GetGameUserSettings())
	{
		Resolutions.AddUnique(Machine->GetScreenResolution());
	}

	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	return Resolutions;
}

TArray<FVBResolutionPresetChoice> UVBOptionsSubsystem::GetResolutionPresets() const
{
	// 엔진 프리셋(BaseScalability.ini [ResolutionQuality]). 목록 함수가 엔진에 묻지 않도록 여기서 전달한다.
	TArray<FVBResolutionPresetChoice> Presets;
	for (const Scalability::FResolutionPreset& Preset : Scalability::GetResolutionPresets())
	{
		Presets.Add({FName(*Preset.Name), Preset.ResolutionQuality});
	}

	return Presets;
}

void UVBOptionsSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	if (NewPlayerController)
	{
		ApplyUserSettings(UVBUserSettingsStore::GetSettingsData(GetLocalPlayer()));
	}
}

void UVBOptionsSubsystem::Deinitialize()
{
	RevertVideoMode();
	if (OriginalApplicationScale.IsSet())
	{
		GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = OriginalApplicationScale.GetValue();
	}

	Super::Deinitialize();
}

void UVBOptionsSubsystem::ApplyUserSettings(const FVBSettingsSaveData& Settings)
{
	ApplyVolumes(Settings);
	ApplyVibration(Settings);

	UUserInterfaceSettings* UISettings = GetMutableDefault<UUserInterfaceSettings>();
	if (!OriginalApplicationScale.IsSet())
	{
		OriginalApplicationScale = UISettings->ApplicationScale;
	}

	UISettings->ApplicationScale = OriginalApplicationScale.GetValue() * Settings.UIScale;
}

void UVBOptionsSubsystem::ApplyVibration(const FVBSettingsSaveData& Settings) const
{
	if (APlayerController* Controller = GetLocalPlayer() ? GetLocalPlayer()->PlayerController.Get() : nullptr)
	{
		ApplyVibrationTo(*Controller, Settings.bVibrationEnabled);
	}
}

void UVBOptionsSubsystem::ApplyVibrationTo(APlayerController& Controller, bool bEnabled)
{
	// PC 의 진동 문이 둘이다: 일반 진동은 bForceFeedbackEnabled, 햅틱은 bDisableHaptics 만 본다(UE 5.8). 둘 다 건다.
	Controller.bForceFeedbackEnabled = bEnabled;
	Controller.SetDisableHaptics(!bEnabled);
}

void UVBOptionsSubsystem::ApplyVolumes(const FVBSettingsSaveData& Settings)
{
	UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	const FAudioDeviceHandle Device = World ? World->GetAudioDevice() : FAudioDeviceHandle();
	if (!Device.IsValid())
	{
		return; // 오디오 없는 월드(헤드리스, 데디 서버)
	}

	const UVBOptionsSettings* Options = GetDefault<UVBOptionsSettings>();
	USoundMix* Mix = Options->VolumeMix.LoadSynchronous();
	if (!Mix)
	{
		VB_LOG(Warning, "옵션: 볼륨 믹스(UVBOptionsSettings::VolumeMix) 미지정 - 볼륨 설정이 소리에 안 먹는다");
		return;
	}

	// 전체 볼륨은 클래스마다 곱한다 - 계층 조정기의 곱셈 규칙에 기대지 않고 한 곳에서 보이게.
	const TPair<const TSoftObjectPtr<USoundClass>*, float> Classes[] =
	{
		{ &Options->MusicClass, Settings.MusicVolume },
		{ &Options->SfxClass, Settings.SfxVolume },
		{ &Options->VoiceClass, Settings.VoiceVolume },
	};
	for (const TPair<const TSoftObjectPtr<USoundClass>*, float>& Pair : Classes)
	{
		if (USoundClass* Class = Pair.Key->LoadSynchronous())
		{
			UGameplayStatics::SetSoundMixClassOverride(World, Mix, Class, Settings.MasterVolume * Pair.Value, 1.0f, 0.0f);
		}
		else
		{
			VB_LOG(Warning, "옵션: 사운드 클래스 미지정(%s) - 그 볼륨이 안 먹는다", *Pair.Key->ToString());
		}
	}

	if (!MixPushedDevices.Contains(Device.GetDeviceID()))
	{
		UGameplayStatics::PushSoundMixModifier(World, Mix);
		MixPushedDevices.Add(Device.GetDeviceID());
	}
}

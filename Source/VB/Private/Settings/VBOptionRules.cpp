// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBOptionRules.h"

#include "SaveSystem/VBSaveGameTypes.h"
#include "Settings/VBOptionsSettings.h"

namespace VBOptionRules
{
	FVBSettingsSaveData Sanitize(FVBSettingsSaveData Settings, const UVBOptionsSettings& Limits)
	{
		for (float* Unit : { &Settings.MasterVolume, &Settings.MusicVolume, &Settings.SfxVolume, &Settings.VoiceVolume,
			&Settings.CameraShakeScale, &Settings.ScreenFlashScale })
		{
			*Unit = FMath::Clamp(*Unit, 0.0f, 1.0f);
		}

		Settings.UIScale = FMath::Clamp(Settings.UIScale, Limits.UIScaleRange.Min, Limits.UIScaleRange.Max);
		Settings.LookSensitivity = FMath::Clamp(Settings.LookSensitivity, Limits.LookSensitivityRange.Min, Limits.LookSensitivityRange.Max);
		// 음수 = '패드는 마우스 감도를 따른다' 라는 저장 상태라 지우지 않는다(-1 로 맞춘다). 고른 값만 범위로 자른다.
		Settings.GamepadLookSensitivity = Settings.GamepadLookSensitivity < 0.0f ? -1.0f
			: FMath::Clamp(Settings.GamepadLookSensitivity, Limits.LookSensitivityRange.Min, Limits.LookSensitivityRange.Max);
		for (float* DeadZone : { &Settings.GamepadLookDeadZone, &Settings.GamepadMoveDeadZone })
		{
			*DeadZone = FMath::Clamp(*DeadZone, Limits.StickDeadZoneRange.Min, Limits.StickDeadZoneRange.Max);
		}

		if (static_cast<uint8>(Settings.SubtitleSize) > static_cast<uint8>(EVBSubtitleSize::Large))
		{
			Settings.SubtitleSize = EVBSubtitleSize::Medium;
		}

		if (static_cast<uint8>(Settings.BloodDisplay) > static_cast<uint8>(EVBBloodDisplay::Off))
		{
			Settings.BloodDisplay = EVBBloodDisplay::Normal;
		}

		FGameplayTagContainer Allowed;
		for (const FVBToggleableInput& Input : Limits.ToggleableInputs)
		{
			if (Settings.ToggleInputs.HasTagExact(Input.InputTag))
			{
				Allowed.AddTag(Input.InputTag);
			}
		}

		Settings.ToggleInputs = MoveTemp(Allowed);
		return Settings;
	}

	bool IsToggleInput(const FGameplayTag& InputTag, bool bAbilityIsToggle, const FVBSettingsSaveData& Settings)
	{
		return bAbilityIsToggle || Settings.ToggleInputs.HasTagExact(InputTag);
	}

	float CombineFrameRateLimits(float UserLimit, TConstArrayView<float> ActiveCaps)
	{
		float Result = 0.0f;
		auto Consider = [&Result](float Limit)
		{
			if (FMath::IsFinite(Limit) && Limit > 0.0f)
			{
				Result = Result > 0.0f ? FMath::Min(Result, Limit) : Limit;
			}
		};
		Consider(UserLimit);
		for (const float Cap : ActiveCaps)
		{
			Consider(Cap);
		}

		return Result;
	}

	bool IsOnBatteryPower(bool bQueryOk, uint8 ACLineStatus, uint8 BatteryFlag)
	{
		// ACLineStatus: 0 꺼짐, 1 연결, 255 모름. BatteryFlag: 128 배터리 없음, 255 모름(그 밖은 잔량, 충전 비트).
		constexpr uint8 ACOffline = 0;
		constexpr uint8 NoBattery = 128;
		constexpr uint8 Unknown = 255;
		return bQueryOk && ACLineStatus == ACOffline && BatteryFlag != NoBattery && BatteryFlag != Unknown;
	}

	float DefaultFrameRateLimit(int32 RefreshRate)
	{
		return RefreshRate > 0 ? static_cast<float>(RefreshRate) : 60.0f;
	}
}

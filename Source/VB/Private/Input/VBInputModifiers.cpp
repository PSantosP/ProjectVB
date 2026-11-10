// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Input/VBInputModifiers.h"

#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

namespace
{
	// 로컬 플레이어의 사람 설정. 없으면(원격, 빙의 전) 구조체 기본값을 쓴다.
	// 월드 문맥은 PlayerInput 을 소유한 PC 다 - 로컬 플레이어는 맵 이동 중 월드가 없어 입력마다 경고가 난다.
	const FVBSettingsSaveData& SettingsFor(const UEnhancedPlayerInput* PlayerInput)
	{
		static const FVBSettingsSaveData Defaults;
		const APlayerController* Controller = PlayerInput ? PlayerInput->GetOuterAPlayerController() : nullptr;
		return Controller && Controller->GetLocalPlayer() && Controller->GetWorld()
			? UVBSaveGameSubsystem::GetSettingsData(Controller) : Defaults;
	}

	// 아래 문턱의 상한. 위 문턱 1.0 과 같으면 분모가 0 이다. 읽기 길은 다듬지 않아 손댄 저장, NaN 도 여기서 자른다.
	constexpr float MaxLowerThreshold = 0.9f;
}

namespace VBInputCalibration
{
	float GamepadLookSensitivity(const FVBSettingsSaveData& Settings)
	{
		return Settings.GamepadLookSensitivity < 0.0f ? Settings.LookSensitivity : Settings.GamepadLookSensitivity;
	}

	float LookSensitivity(const FVBSettingsSaveData& Settings, EVBLookDevice Device)
	{
		return Device == EVBLookDevice::Gamepad ? GamepadLookSensitivity(Settings) : Settings.LookSensitivity;
	}

	FVector2D ApplyLook(const FVector2D& Value, float Sensitivity, bool bInvertY)
	{
		return FVector2D(Value.X * Sensitivity, Value.Y * Sensitivity * (bInvertY ? -1.0f : 1.0f));
	}

	FVector ApplyRadialDeadZone(const FVector& Value, EInputActionValueType ValueType, float LowerThreshold)
	{
		constexpr float UpperThreshold = 1.0f;
		const float Lower = FMath::Clamp(LowerThreshold, 0.0f, MaxLowerThreshold);
		// 엔진 UInputModifierDeadZone::ModifyRaw_Implementation 의 Radial 갈래와 같은 식.
		auto Scale = [Lower](float Axis)
		{
			return FMath::Min(1.0f, FMath::Max(0.0f, FMath::Abs(Axis) - Lower) / (UpperThreshold - Lower)) * FMath::Sign(Axis);
		};
		switch (ValueType)
		{
		case EInputActionValueType::Axis3D:
			return Value.GetSafeNormal() * Scale(Value.Size());
		case EInputActionValueType::Axis2D:
			return Value.GetSafeNormal2D() * Scale(Value.Size2D());
		case EInputActionValueType::Axis1D:
			return FVector(Scale(Value.X), Value.Y, Value.Z);
		default:
			return Value;
		}
	}
}

FInputActionValue UVBInputModifier_LookCalibration::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput,
	FInputActionValue CurrentValue, float DeltaTime)
{
	if (CurrentValue.GetValueType() == EInputActionValueType::Boolean)
	{
		return CurrentValue;
	}

	const FVBSettingsSaveData& Settings = SettingsFor(PlayerInput);
	const FVector Value = CurrentValue.Get<FVector>();
	const FVector2D Look = VBInputCalibration::ApplyLook(FVector2D(Value.X, Value.Y),
		VBInputCalibration::LookSensitivity(Settings, Device), Settings.bInvertYAxis);
	return FInputActionValue(CurrentValue.GetValueType(), FVector(Look.X, Look.Y, Value.Z));
}

FInputActionValue UVBInputModifier_StickDeadZone::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput,
	FInputActionValue CurrentValue, float DeltaTime)
{
	const EInputActionValueType ValueType = CurrentValue.GetValueType();
	if (ValueType == EInputActionValueType::Boolean)
	{
		return CurrentValue;
	}

	const FVBSettingsSaveData& Settings = SettingsFor(PlayerInput);
	const float Lower = Stick == EVBStick::Look ? Settings.GamepadLookDeadZone : Settings.GamepadMoveDeadZone;
	return FInputActionValue(ValueType, VBInputCalibration::ApplyRadialDeadZone(CurrentValue.Get<FVector>(), ValueType, Lower));
}

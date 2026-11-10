// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputModifiers.h"
#include "VBInputModifiers.generated.h"

struct FVBSettingsSaveData;

// 시점 입력의 장치. 감도 값을 고르는 데만 쓴다.
UENUM(BlueprintType)
enum class EVBLookDevice : uint8
{
	Mouse,
	Gamepad,
	Touch, // 터치 시점 패드. 마우스 감도를 쓴다.
};

UENUM(BlueprintType)
enum class EVBStick : uint8
{
	Move,
	Look,
};

// 입력 보정 계산. 월드 없이 시험할 수 있게 보정기에서 분리했다.
namespace VBInputCalibration
{
	// 패드 시점 감도. 저장값이 음수(아직 안 고름)면 마우스 감도를 따른다.
	VB_API float GamepadLookSensitivity(const FVBSettingsSaveData& Settings);

	VB_API float LookSensitivity(const FVBSettingsSaveData& Settings, EVBLookDevice Device);

	// 감도를 곱하고 Y 를 뒤집는다.
	VB_API FVector2D ApplyLook(const FVector2D& Value, float Sensitivity, bool bInvertY);

	// 엔진 DeadZone 의 원형 방식(위 문턱 1.0). 아래 문턱이 [0, 1) 밖이면 0 - 엔진은 실행 중 분모 0 을 막지 않는다.
	VB_API FVector ApplyRadialDeadZone(const FVector& Value, EInputActionValueType ValueType, float LowerThreshold);
}

/**
 * 시점 감도, Y반전. IMC 의 Look 매핑 맨 끝에 단다.
 * 설정을 매 입력마다 읽어 옵션 변경이 다음 입력부터 걸린다.
 */
UCLASS(NotBlueprintable, meta=(DisplayName="VB Look Calibration"))
class VB_API UVBInputModifier_LookCalibration : public UInputModifier
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Input")
	EVBLookDevice Device = EVBLookDevice::Mouse;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue,
		float DeltaTime) override;
};

/** 패드 스틱 데드존. 엔진 원형 DeadZone 과 같은 식이고 아래 문턱만 사용자 설정에서 온다. */
UCLASS(NotBlueprintable, meta=(DisplayName="VB Stick Dead Zone"))
class VB_API UVBInputModifier_StickDeadZone : public UInputModifier
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Input")
	EVBStick Stick = EVBStick::Move;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue,
		float DeltaTime) override;
};

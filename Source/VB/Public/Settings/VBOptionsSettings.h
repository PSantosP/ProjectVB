// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Math/Interval.h"
#include "Settings/VBOptionTypes.h"
#include "VBOptionsSettings.generated.h"

class USoundClass;
class USoundMix;

// 누르고 있기/토글을 고를 수 있는 입력 하나와 옵션 화면에 보일 이름.
USTRUCT()
struct FVBToggleableInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|Options", meta=(Categories="Input"))
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, Category="VB|Options")
	FText Label;
};

/**
 * 옵션의 범위, 단계, 연결 자산. Project Settings > Game > VB Options, Config/DefaultGame.ini.
 * 사용자가 고른 값은 여기 없다(FVBSettingsSaveData, UGameUserSettings) - 여기는 무엇을 고를 수 있는지다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Options"))
class VB_API UVBOptionsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 볼륨을 거는 믹스와 클래스. 전체 볼륨은 각 클래스 볼륨에 곱해진다(계층을 두지 않는다 - 곱이 한 곳에서 보이게).
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Audio")
	TSoftObjectPtr<USoundMix> VolumeMix;

	UPROPERTY(config, EditAnywhere, Category="VB|Options|Audio")
	TSoftObjectPtr<USoundClass> MusicClass;

	UPROPERTY(config, EditAnywhere, Category="VB|Options|Audio")
	TSoftObjectPtr<USoundClass> SfxClass;

	UPROPERTY(config, EditAnywhere, Category="VB|Options|Audio")
	TSoftObjectPtr<USoundClass> VoiceClass;

	// 프레임 제한 선택지. 0 = 무제한.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Display")
	TArray<float> FrameRateLimits = { 30.0f, 60.0f, 120.0f, 144.0f, 0.0f };

	// 정지 화면(메뉴, 일시정지)이 떠 있는 동안의 프레임 제한. 0 = 끔. 사용자가 더 낮게 골랐으면 그 값이 남는다(VBOptionRules).
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Display", meta=(ClampMin="0"))
	float MenuFrameRateLimit = 60.0f;

	// 게임 창이 뒤에 있을 때의 상한. 0 = 끔. Standalone, 클라이언트에서만, PIE 에서는 vb.FrameBudget.ApplyInPIE 일 때만.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Display", meta=(ClampMin="0"))
	float FrameRateLimitWhenBackgrounded = 30.0f;

	// 배터리로만 돌 때(전원 연결 끊김)의 상한. 0 = 끔. 적용 범위는 위와 같다.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Display", meta=(ClampMin="0"))
	float FrameRateLimitOnBattery = 60.0f;

	// 전원 상태를 다시 보는 간격(실시간 초). 엔진에 전원 바뀜 알림이 없다(이 Windows 대상에서 찾지 못함).
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Display", meta=(ClampMin="1.0", Units="s"))
	float PowerPollSeconds = 5.0f;

	// 해상도, 창 모드를 바꾼 뒤 유지를 기다리는 시간. 지나면 되돌린다.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Display", meta=(ClampMin="3", Units="s"))
	float VideoConfirmSeconds = 15.0f;

	// 자막 글자 크기. 없는 단계는 WBP 에 저작한 크기를 그대로 쓴다.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Subtitles")
	TMap<EVBSubtitleSize, int32> SubtitleFontSizes;

	UPROPERTY(config, EditAnywhere, Category="VB|Options|Accessibility")
	FFloatInterval UIScaleRange = FFloatInterval(0.8f, 1.5f);

	UPROPERTY(config, EditAnywhere, Category="VB|Options|Controls")
	FFloatInterval LookSensitivityRange = FFloatInterval(0.1f, 3.0f);

	// 패드 스틱 데드존(아래 문턱) 범위. 위 문턱은 1.0 이라 상한이 1 에 닿으면 데드존 식의 분모가 0 이 된다.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Controls")
	FFloatInterval StickDeadZoneRange = FFloatInterval(0.0f, 0.9f);

	// 누르고 있기/토글을 고를 수 있는 입력. 옵션 화면 조작 탭에 이 순서로 나온다.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Controls")
	TArray<FVBToggleableInput> ToggleableInputs;

	// 피 표현 다른 색, 끄기일 때 타격 이펙트에 넣는 색. 키 = Niagara User 파라미터 이름, 읽는 쪽 = UVBGC_MeleeImpact.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Gameplay")
	TMap<FName, FLinearColor> AlternateBloodColors;

	// 피 표현 다른 색의 바닥 자국 색. 데칼 재질 Tint 로 들어간다.
	UPROPERTY(config, EditAnywhere, Category="VB|Options|Gameplay")
	FLinearColor AlternateBloodDecalColor = FLinearColor(0.35f, 0.55f, 0.9f, 1.0f);
};

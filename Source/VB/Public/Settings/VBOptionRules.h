// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

struct FVBSettingsSaveData;
class UVBOptionsSettings;

// 사용자 설정의 판정. 순수 함수라 월드 없이 시험한다.
namespace VBOptionRules
{
	// 범위 밖 값(옛 슬롯, 손으로 고친 파일)을 고를 수 있는 범위로 되돌린다. 토글 입력은 고를 수 있는 것만 남긴다.
	VB_API FVBSettingsSaveData Sanitize(FVBSettingsSaveData Settings, const UVBOptionsSettings& Limits);

	// 이 입력을 토글로 다루는가 = 어빌리티가 원래 토글이거나(앉기) 사용자가 토글을 골랐다.
	VB_API bool IsToggleInput(const FGameplayTag& InputTag, bool bAbilityIsToggle, const FVBSettingsSaveData& Settings);

	// 사용자 제한과 지금 걸린 상한들(메뉴, 창이 뒤, 배터리)을 합친다: 양수인 것 중 가장 작은 값, 하나도 없으면 0(무제한).
	//   0, 음수, NaN, 무한은 '그 상한 없음' 이다.
	VB_API float CombineFrameRateLimits(float UserLimit, TConstArrayView<float> ActiveCaps);

	// 전원 상태 조회 결과가 '배터리로만 돈다' 인가. 충전 중, AC 연결, 배터리 없음, 모름, 조회 실패는 아니다 -
	//   엔진의 IsRunningOnBattery 는 충전 중도 참이라 쓰지 않는다. 값은 Windows SYSTEM_POWER_STATUS 의 뜻.
	VB_API bool IsOnBatteryPower(bool bQueryOk, uint8 ACLineStatus, uint8 BatteryFlag);

	// 처음 켤 때의 프레임 제한 = 모니터 주사율. 주사율을 모르면(0 이하) 60 - 모를 때 무제한으로 떨어지지 않게.
	VB_API float DefaultFrameRateLimit(int32 RefreshRate);
}

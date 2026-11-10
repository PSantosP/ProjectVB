// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBCombatTypes.generated.h"

/**
 * 전투 그룹 안에서 적 하나가 맡는 역할. 상호 배타라 태그가 아닌 열거형이고 BT 가 직접 분기한다.
 * 블랙보드에 uint8 로 저장되므로 새 값은 항상 끝에 더한다.
 */
UENUM(BlueprintType)
enum class EVBCombatRole : uint8
{
	Idle UMETA(DisplayName = "Idle (전투 미참여)"),
	Approaching UMETA(DisplayName = "Approaching (토큰 보유, 사거리 밖)"),
	Attacker UMETA(DisplayName = "Attacker (토큰 보유, 사거리 안)"),
	Surrounder UMETA(DisplayName = "Surrounder (토큰 미보유, 링 슬롯 대기)"),
	// 측면 가중 역할. 아직 배정하지 않는다.
	Flanker UMETA(DisplayName = "Flanker (측면 가중 - 미배정)"),
};

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBCombatSettings.generated.h"

/**
 * 적 그룹 전투 조율의 프로젝트 전역 설정(Project Settings > Game > VB Combat).
 * 소비자가 월드당 하나뿐이라 DataAsset 포인터 대신 GetDefault 로 닿는 설정에 둔다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Combat"))
class VB_API UVBCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 한 타겟에 동시에 붙을 수 있는 공격자 총 코스트. 크게(99) 두면 그룹 조율이 사실상 꺼진다.
	UPROPERTY(config, EditAnywhere, Category="VB|AI|Coordination", meta=(ClampMin="0"))
	int32 MaxAttackTokens = 2;

	// 타겟 주위 대기 링의 슬롯 개수(FR3). 슬롯 수가 대기 인원보다 적으면 남는 적은 슬롯 없이 제자리 대기한다.
	UPROPERTY(config, EditAnywhere, Category="VB|AI|Coordination", meta=(ClampMin="1"))
	int32 SurroundSlotCount = 8;

	// 대기 반경은 사거리에 상대적이라 UVBEnemyConfig::WaitingStandoff 가 아키타입별로 든다.

	// 역할/슬롯 재계산 주기(초). Tick 대신 그룹당 타이머 하나가 돈다.
	UPROPERTY(config, EditAnywhere, Category="VB|AI|Coordination", meta=(ClampMin="0.05"))
	float SlotRecomputeInterval = 0.5f;

	// 공격 한 번(예고+스윙+후딜)의 토큰 임대 상한(초). 초과하면 Warning 을 남기고, 공격 태그가 내려갔는데 해제가 없을 때만 회수한다.
	// 접근 구간의 상한은 UVBEnemyConfig::MaxApproachHoldSeconds 다.
	UPROPERTY(config, EditAnywhere, Category="VB|AI|Coordination", meta=(ClampMin="0.5"))
	float MaxTokenHoldSeconds = 4.0f;

	// 링 슬롯을 네비메시로 투영할 질의 범위(cm). 실패하면 원래 좌표를 쓴다. 벽 안쪽 슬롯으로 비비는 것을 막는다.
	UPROPERTY(config, EditAnywhere, Category="VB|AI|Coordination", meta=(ClampMin="0.0"))
	float SlotNavProjectionExtent = 200.0f;

	// 무엇이 무엇을 막고 끊는가의 표. 플레이어와 적이 같은 표를 쓴다 - 캐릭터 클래스마다 두면 새 클래스가 빠뜨린다.
	// 비어 있으면 능력이 하나도 안 켜진다(사망 막기 없이 돌 수 없다).
	UPROPERTY(config, EditAnywhere, Category="VB|Combat")
	TSoftObjectPtr<class UVBCombatRuleTable> CombatRules;
};

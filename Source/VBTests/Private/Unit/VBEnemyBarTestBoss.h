// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "Enemy/VBBossBase.h"
#include "Data/VBEnemyConfig.h"
#include "VBEnemyBarTestBoss.generated.h"

// AVBBossBase 는 Abstract 다. 보스 바 시험(VBEnemyBarTests)이 스폰할 구체 클래스 - 동작은 베이스 그대로.
UCLASS()
class AVBEnemyBarTestBoss : public AVBBossBase
{
	GENERATED_BODY()

public:
	AVBEnemyBarTestBoss(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
	// 보스 공격 행 시험(VBBossAttackRulesTests)이 설정 자산을 꽂는다.
	void Configure(UVBEnemyConfig* Config) { EnemyConfig = Config; }
};

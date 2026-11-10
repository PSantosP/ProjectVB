// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "Enemy/VBEnemyBase.h"
#include "Data/VBEnemyConfig.h"
#include "VBCombatAuditTestAbility.generated.h"

// 질의 전용 fixture - 실제 lease 가 풀린 뒤에만 준비됨이 true 가 된다.
UCLASS()
class UVBCombatAuditTestAbility : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBCombatAuditTestAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};

// 보스 상황 줄 ability (Input.Attack.Armored) - base 가 허락하면 언제든 켜진다.
UCLASS()
class UVBBossRowTestAbility : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBBossRowTestAbility();
};

// EnemyBase 는 abstract 라 이 구체 fixture 가 그 ASC 와 combat component 를 그대로 쓴다.
UCLASS()
class AVBCombatAuditTestEnemy : public AVBEnemyBase
{
	GENERATED_BODY()

public:
	AVBCombatAuditTestEnemy(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
	void Configure(UVBEnemyConfig* Config) { EnemyConfig = Config; }
};

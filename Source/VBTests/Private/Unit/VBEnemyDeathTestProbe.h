// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBBossBase.h"
#include "Enemy/VBEnemyDeathPresentationComponent.h"
#include "GameplayEffect.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBEnemyDeathTestProbe.generated.h"

// 시험 적이 실제로 보낸 죽음 스냅샷을 세는 자리. 이 네이티브 fixture 에서는 skeletal 물리를 돌리지 않고 RPC 의 전달 값만 본다.
UCLASS()
class UVBEnemyDeathTestPresentation : public UVBEnemyDeathPresentationComponent
{
	GENERATED_BODY()
protected:
	virtual void MulticastPresent_Implementation(const FVBEnemyDeathState& State) override;
};

UCLASS()
class UVBEnemyDeathTestReputationEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBEnemyDeathTestReputationEffect();
};

UCLASS()
class UVBEnemyDeathTestHealthEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBEnemyDeathTestHealthEffect();
};

UCLASS()
class UVBEnemyDeathTestAbility : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBEnemyDeathTestAbility();
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class AVBEnemyDeathTestEnemy : public AVBEnemyBase
{
	GENERATED_BODY()
public:
	AVBEnemyDeathTestEnemy(const FObjectInitializer& ObjectInitializer);
	void Configure(UVBEnemyConfig* Config);
	void SetInteractionMomentum(const FVector& Velocity);
	TArray<FName> Steps;
	int32 BeforeCalls = 0;
	int32 AfterCalls = 0;
	int32 PresentCalls = 0;
	int32 MulticastCalls = 0;
	int32 KillEvents = 0;
	int32 KilledEffects = 0;
	bool bKillEventPrecededEffect = false;
	bool bPresentationReturned = false;
	bool bDownedAtBefore = false;
	bool bCancelledAtAfter = false;
	FVBEnemyDeathState SentState;
protected:
	virtual void BeforeDeathTag(const FVBDeathFacts& Facts) override;
	virtual void AfterDeathTag(const FVBDeathFacts& Facts) override;
	virtual void PresentDeath(const FVBDeathFacts& Facts) override;
};

USTRUCT()
struct FVBEnemyDeathTestKilledEffect : public FVBEffect
{
	GENERATED_BODY()
	virtual void Apply(const FVBRuleContext& Context) const override;
};

UCLASS()
class AVBEnemyDeathTestBoss : public AVBBossBase
{
	GENERATED_BODY()
public:
	AVBEnemyDeathTestBoss(const FObjectInitializer& ObjectInitializer);
	void Configure(UVBEnemyConfig* Config);
	TFunction<void()> DuringBefore;
	int32 BeforeCalls = 0;
	int32 PresentCalls = 0;
	int32 MulticastCalls = 0;
	bool bPresentationReturned = false;
	bool bSeedStaleEngagement = false;
protected:
	virtual void BeforeDeathTag(const FVBDeathFacts& Facts) override;
	virtual void AfterDeathTag(const FVBDeathFacts& Facts) override;
	virtual void PresentDeath(const FVBDeathFacts& Facts) override;
};

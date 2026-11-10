// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "AbilitySystem/Abilities/VBTelegraphGrant.h"
#include "VBGA_EnemyMeleeAttack.generated.h"

class UAnimMontage;
class URootMotionModifier;
class UMotionWarpingComponent;
class UVBAttackConfig;

/**
 * 적 전용 근접 공격. 베이스와의 차이는 스윙 진입을 TelegraphDuration 만큼 미루는 예고 단계와 접근 워프다.
 * 예고를 BT 가 아니라 GA 안에 두는 이유: 토큰 보유 구간 = GA 수명이 되고 취소 지점이 하나다.
 * 쿨다운은 예고 시작부터 돈다(CommitAbility 가 ExecuteAbility 앞에서 통과).
 */
UCLASS(Abstract)
class VB_API UVBGA_EnemyMeleeAttack : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()

public:
	UVBGA_EnemyMeleeAttack();

	static bool IsApproachWindowPending(const URootMotionModifier* Modifier, const UAnimMontage* Montage);
	static float MeasureApproachWindowLunge(const URootMotionModifier* Modifier, float MeshScale);

protected:
	// Super 를 즉시 부르지 않고 예고 시간만큼 미룬다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 예고 상태를 정리한다. 취소(경직)로 들어와도 여기로 수렴한다. 예고 지연 태스크는 엔진이 함께 끝낸다.
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// 거리에 맞는 변형. 워프 허용비 안에 드는 것 중 전진이 가장 긴 것, 없으면 |ln(전진/필요)| 최소. 타겟이 없으면 베이스 순환.
	virtual int32 SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount) override;

private:
	// 다운 반격이 아니면 경직 태그를 감시한다.
	void WatchStaggerUnlessDownCounter();

	// 히트리액트 태그를 감시한다. 태그가 이미 있으면 이 안에서 어빌리티가 끝날 수 있다.
	void WatchHitReact();

	// 예고 태그와 예고 큐를 붙이고 예고 시작 이벤트를 쏜다.
	void RaiseTelegraph(AActor* Avatar, const UVBAttackConfig* Pending, float TelegraphDuration);

	// 연출 전용 예고 몽타주를 튼다.
	void PlayTelegraphMontage(UAnimMontage* TelegraphMontage);

	// 예고 길이만큼 기다린 뒤 스윙한다. 월드가 없으면 즉시 스윙한다.
	void WaitTelegraphThenSwing(float TelegraphDuration);

	// 예고가 끝난 뒤 실제 스윙 진입. 베이스 ExecuteAbility 를 부른다(FWaitDelayDelegate 가 DYNAMIC 이라 UFUNCTION).
	UFUNCTION()
	void BeginSwing();

	// 경직 태그가 붙는 즉시 이 공격을 끝낸다. 규칙 표는 신규 활성화만 막아 진행 중인 예고는 여기서 끊는다.
	UFUNCTION()
	void OnStaggeredDuringAttack();

	// 예고 중에 맞으면(히트리액트가 켜지면) 이 공격을 끝낸다. 슈퍼아머 공격은 히트리액트가 안 켜져 예외다.
	UFUNCTION()
	void OnHitDuringAttack();

	// 전투 컴포넌트의 타겟으로 접근 워프를 세운다. 베이스의 락온 경로는 플레이어 전용이라 적에서는 안 세운다.
	void SetWarpTargetToCombatTarget(const URootMotionModifier* Modifier);

	UFUNCTION()
	void OnApproachWarpUpdate(UMotionWarpingComponent* Component);

	TWeakObjectPtr<UMotionWarpingComponent> ApproachWarpComponent;
	TSet<TWeakObjectPtr<URootMotionModifier>> CapturedApproachWindows;
	FVBTelegraphGrant Telegraph;
};

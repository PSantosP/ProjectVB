// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h" // OnAttackingTagChanged 가 FGameplayTag 를 값으로 받는다
#include "AI/VBArmoredCounterTracker.h"
#include "AI/VBCombatTypes.h"
#include "VBEnemyCombatComponent.generated.h"

class UVBEnemyConfig;
class UVBCombatCoordinatorSubsystem;
class UAbilitySystemComponent;
struct FGameplayEventData;

/**
 * 적 하나의 전투 조율 상태. 전투 타겟만 소유하고 역할/토큰/슬롯은 코디네이터에 위임 조회한다.
 * 서버 전용이며 복제하지 않는다. Tick 없음 - 관측은 BTService, 조율은 코디네이터 타이머가 한다.
 */
UCLASS()
class VB_API UVBEnemyCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBEnemyCombatComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 블랙보드 TargetActor 를 코디네이터 등록에 반영하는 유일한 경로. null 이면 전투 이탈. 매 주기 부르는 멱등 함수다
	void SetCombatTarget(AActor* NewTarget);

	// 전투 이탈. SetCombatTarget(nullptr) 에 위임한다.
	void LeaveCombat();

	// 코디네이터 위임 조회. 캐시하지 않는다.
	EVBCombatRole GetRole() const;
	bool HasAttackToken() const;
	FVector GetSlotLocation() const;
	AActor* GetCombatTarget() const { return CombatTarget.Get(); }

	// 이 InputTag 의 스펙을 지금 켤 수 있는가(쿨다운, 코스트, 차단 태그)
	bool CanActivateAbilityByInputTag(const FGameplayTag& InputTag) const;
	// 마지막 공격 시작 월드 시각(서버). 전투 진입도 시작으로 친다
	double GetLastAttackStartTime() const { return LastAttackStartTime; }

	// 전투 중인가(서버). 바뀔 때만 OnInCombatChanged 로 알린다
	bool IsInCombat() const { return bInCombat; }
	DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnInCombatChanged, bool /*bInCombat*/);
	FVBOnInCombatChanged OnInCombatChanged;

	// 계획된 다음 공격. 토큰을 받는 순간 변형을 고르고 시작 거리(클립 전진 + 멈추는 거리)를 정한다. 공격 GA 는 ConsumePlannedVariantIndex 로 같은 변형을 튼다
	void PlanNextAttack(const FVector& TargetLocation);
	void ClearPlannedAttack();
	int32 ConsumePlannedVariantIndex();

	// 시작 거리를 블랙보드에 바로 쓴다(계획이 바뀐 순간을 MoveTo 시작보다 앞세운다)
	void PushStartDistanceToBlackboard() const;
	// 계획이 없으면 AttackCommitDistance 를 쓴다
	float GetAttackStartDistance() const;
	float GetAttackReleaseDistance() const;

	// 지금 공격 GA 를 켤 수 있는가. 엔진 CanActivateAbility 에 위임한다
	bool IsReadyToAttack() const;

	// 이 적이 쓰는 공격 GA 의 InputTag. 코디네이터와 BT 태스크가 같은 답을 보도록 해석은 여기 하나다
	FGameplayTag GetAttackInputTag() const;

	// 지금 공격 GA 가 활성인가. 구독 실패에 안전하도록 래치가 아닌 ASC 태그로 답한다
	bool IsAttacking() const;

	// 지금 예고(텔레그래프) 중인가. 코디네이터가 토큰 배분을 잠글 때 쓴다
	bool IsTelegraphing() const;

	// 슈퍼아머 반격을 원하는가. 반격 태그가 있고, 창 안에 문턱만큼 맞았고, 그 GA 를 지금 켤 수 있어야 한다
	bool WantsArmoredCounter() const;

	// 공격 능력 계약 검증(서버, 1회). 부여 루프 뒤라야 하므로 AVBEnemyBase::BeginPlay 가 부른다
	void ValidateAttackAbilityContract() const;

	// 이 적의 튜닝. 소유자 EnemyConfig 를 위임 조회한다(null 아님)
	const UVBEnemyConfig* GetCombatConfig() const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBCombatLeaseReproAccess;
#endif
	// 코디네이터가 CombatTarget 약참조 원본을 그룹 키로 읽는다(타겟 파괴 후에도 그룹을 찾는다)
	friend class UVBCombatCoordinatorSubsystem;

	// State.Combat.Attacking 카운트 변화. 하강 에지가 토큰 해제의 유일한 정상 지점이다.
	void OnAttackingTagChanged(const FGameplayTag Tag, int32 NewCount);

	// State.Combat.HitReacting 상승 에지 = 히트리액트가 켜진 피격 한 대. 반격 근거를 기록하고 어느 타인지는 OnCombatHitEvent 가 채운다
	void OnHitReactingTagChanged(const FGameplayTag Tag, int32 NewCount);

	// Event.Combat.Hit 구독. 방금 기록한 줄에 어느 능력의 몇 번째 타인지 채운다
	void OnCombatHitEvent(const FGameplayEventData* Payload);

	UAbilitySystemComponent* GetOwnerASC() const;

	// 내가 지금 노리는 대상. 이 컴포넌트가 소유하는 유일한 전투 사실이다
	TWeakObjectPtr<AActor> CombatTarget;
	// 마지막으로 알린 있음/없음. SetCombatTarget 만 쓴다
	bool bInCombat = false;

	// 계획된 공격(서버). INDEX_NONE 또는 음수 = 계획 없음
	int32 PlannedVariantIndex = INDEX_NONE;
	float PlannedStartDistance = -1.0f;
	// 같은 공격이 연달아 나오지 않게 도는 커서. 리셋하지 않는다
	int32 VariantPlanCursor = 0;

	// ASC 태그와 이벤트 구독 핸들(서버). 무효면 구독 안 함
	FDelegateHandle AttackingTagHandle;
	FDelegateHandle HitReactingTagHandle;
	FDelegateHandle CombatHitEventHandle;

	FVBArmoredCounterTracker ArmoredCounter;

	double LastAttackStartTime = 0.0;

	// HitReacting 상승 에지 검출용 래치
	bool bWasHitReacting = false;

	// Attacking 하강 에지 검출용 래치. 상승 없이 오는 첫 콜백을 무시한다
	bool bWasAttacking = false;
};

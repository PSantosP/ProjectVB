// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/VBBossBaseRowState.h"
#include "Enemy/VBEnemyBase.h"
#include "VBBossBase.generated.h"

class UVBBossConfig;
class UAnimMontage;
struct FVBBossPhase;

/**
 * 보스 공통 클래스. AVBEnemyBase 위에 체력 문턱 페이즈(공격 끊기, 전환 몽타주, 공격 태그 교체)와 상황 공격을 얹는다.
 * 페이즈 판정은 서버 전용이다. OnRep 은 원격 클라 연출만 맡고, 리슨 서버는 EnterPhase 에서 연출을 직접 튼다.
 */
UCLASS(Abstract)
class VB_API AVBBossBase : public AVBEnemyBase
{
	GENERATED_BODY()

public:
	AVBBossBase(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 지금 쓸 공격 태그: 토큰을 보유한 계획 동안 고른 상황 행이 있으면 그것, 아니면 페이즈 기본(0 = AttackAbilityInputTag, N = AdditionalPhases[N-1]).
	virtual FGameplayTag GetCurrentAttackInputTag() const override;
	// 상황(등 뒤, 멀다, 몰아 맞음)을 보고 이번 계획의 행을 고른다(VBBossAttackRules). 반격이 이기는 계획에서는 고르지 않는다.
	virtual void ChooseNextAttack() override;
	virtual void ForgetChosenAttack() override;
	// 고른 행으로 공격이 시작됐을 때만 그 행의 쿨다운을 건다 - 회수, 반격으로 안 쓴 행은 쿨다운을 쓰지 않는다.
	virtual void NotifyAttackStarted() override;

	// 0 = 첫 페이즈. UVBBossConfig::AdditionalPhases 와의 대응은 그 헤더에 있다.
	int32 GetCurrentPhaseIndex() const { return CurrentPhaseIndex; }

	// 전환 몽타주가 도는 중인가(서버 전용 사실. 클라는 태그도 몽타주도 연출로만 받는다).
	bool IsInPhaseTransition() const { return bPhaseTransitionActive; }

	// EnemyConfig 를 보스 설정으로. 보스 BP 에 잡몹 설정이 꽂혀 있으면 null 이다 - 그때는 페이즈가 없고 BeginPlay 가 1회 경고한다.
	const UVBBossConfig* GetBossConfig() const;

	// 싸우는 중인가(복제) - 보스 바가 이것을 본다. 서버가 전투 여부와 생존으로 정한다.
	bool IsEngaged() const { return bEngaged; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 체력 변화로 페이즈를 판정한다. 사망 래치가 중첩된 사망 쓰기를 거른다.
	void OnHealthChanged(const struct FOnAttributeChangeData& Data);

	virtual void BeforeDeathTag(const FVBDeathFacts& Facts) override;
	virtual void PresentDeath(const FVBDeathFacts& Facts) override;

	// 지금 페이즈. 서버가 EnterPhase 로 올리고, 원격 클라는 OnRep 으로 연출만 받는다(노출 키워드가 없어 Category 없음).
	UPROPERTY(ReplicatedUsing=OnRep_CurrentPhaseIndex)
	int32 CurrentPhaseIndex = 0;

	// 이번 전환이 연출 없이 페이즈만 바꾸는가(엎드린 채 문턱을 지남). 서버가 정하고 OnRep 도 같은 값을 본다.
	// 클라가 Downed 태그를 따로 읽으면 인덱스와 도착 순서가 갈려 같은 액터 프로퍼티로 싣는다.
	UPROPERTY(Replicated)
	bool bPhaseTransitionSilent = false;

	UFUNCTION()
	void OnRep_CurrentPhaseIndex();

	UPROPERTY(ReplicatedUsing=OnRep_Engaged)
	bool bEngaged = false;

	// 원격 클라. 리슨 호스트에서는 OnRep 이 안 불리므로 서버 SetEngaged 가 같은 알림을 직접 한다.
	UFUNCTION()
	void OnRep_Engaged();

private:
	friend class FVBEnemyBarTestAccess;

	// 서버 전용. 전투 컴포넌트의 있음/없음을 받아 살아 있을 때만 bEngaged 로 올린다.
	void HandleInCombatChanged(bool bInCombat);
	void SetEngaged(bool bNewEngaged);
	// 이 기계의 보스 목록(UVBBossEncounterSubsystem)에 지금 bEngaged 를 알린다. 서버, 클라 공통.
	void PublishEngaged();
	FDelegateHandle InCombatChangedHandle;
	FDelegateHandle HealthChangedHandle;

	FVBBossRowState RowState;
	FGameplayTag GetPhaseDefaultAttackInputTag() const;

	// 서버 전용. 페이즈를 올리고 전환을 시작한다. 두 문턱을 한 대에 지나면 마지막 것 하나로 들어간다(연출은 한 번).
	void EnterPhase(int32 NewPhaseIndex);

	// 서버 전용. 전환 태그, 슈퍼아머를 걸고 몽타주를 튼다. 몽타주가 없거나 재생이 실패하면 즉시 끝낸다.
	void BeginPhaseTransition(const FVBBossPhase& Phase);

	// 서버 전용. 멱등 - 몽타주 종료 델리게이트, 사망, 재생 실패 세 경로가 여기로 모인다.
	void EndPhaseTransition();

	// Serial 이 현재 전환 세대와 다르면 무시한다 - 앞 몽타주의 종료 알림이 늦게 와 새 전환을 벗기지 않게(같은 몽타주일 수 있다).
	void OnTransitionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Serial);

	// 연출만. 서버(EnterPhase)와 원격 클라(OnRep) 양쪽이 부른다. 반환 = 실제 재생 길이(0 이면 실패).
	float PlayTransitionMontage(const FVBBossPhase& Phase);

	// 서버 전용 래치들. 태그를 걸었을 때만 떼기 위해 둔다.
	bool bPhaseTransitionActive = false;
	bool bGrantedTransitionSuperArmor = false;

	// 전환 세대. BeginPhaseTransition 마다 1 오른다. OnTransitionMontageEnded 가 자기 세대인지 대조한다.
	int32 PhaseTransitionSerial = 0;

	// 전환 계측(서버 전용). 종료 로그가 몽타주가 끝까지 돌았는지, 그동안 보스가 움직였는지 답한다.
	FVector PhaseTransitionStartLocation = FVector::ZeroVector;
	double PhaseTransitionStartTime = 0.0;
};

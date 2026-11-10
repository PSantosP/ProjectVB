// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBAirSuspension.h"
#include "AbilitySystem/Abilities/VBDownCounterWatch.h"
#include "AbilitySystem/Abilities/VBGA_Stagger.h"
#include "ActiveGameplayEffectHandle.h"
#include "Data/VBHitReactConfig.h" // FVBKnockdownClips - 이번 다운의 클립 묶음을 값으로 든다
#include "VBGA_Knockdown.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
struct FAbilityEndedData;
struct FGameplayEventData;
struct FVBKnockdownSet;
enum class EVBHitDirection : uint8;

// UVBGA_Knockdown - 경직(UVBGA_Stagger) 파생 다운 어빌리티. 넘어질 수 있으면 넘어짐 -> 엎드린 대기 -> 기상을, 못 넘어지면 Super 의 서 있는 경직을 돈다
// 진입은 기절 게이지 소진(PoiseBroken, 쿨다운, bFallOnPoiseBreak 를 봄)과 넘어뜨리는 타격(KnockedDown) 둘이다
// State.Combat.Staggered 를 그대로 들어 BT 의 이동, 공격 분기를 닫는다. 몽타주 종료는 BlendOut, Interrupted, Cancelled 만 건다 (Completed 는 두 번 튼다)
UCLASS(Abstract)
class VB_API UVBGA_Knockdown : public UVBGA_Stagger
{
	GENERATED_BODY()

public:
	UVBGA_Knockdown();
	/** 이 피해자의 내려찍기 피격 동작 */
	UAnimMontage* GetAirSlamMontage() const;

protected:
	// 가져간 입력(ClaimedInputTags = 회피)이 오면 빠져나오기를 묻는다.
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	// 넘어질 수 있으면 다운 흐름을, 못 넘어지면 Super 의 서 있는 경직을 돈다.
	virtual void RunReaction(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// 넘어뜨리는 타격(Event.Combat.KnockedDown)은 KnockdownSet 이 저작돼 있을 때만 받는다 - 거절하면 허브가 무거운 피격으로 내린다
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayEventData* Payload) const override;

private:
	// 엎드린 채 회피 입력 - 빠져나올 수 있으면 빠른 기상으로 넘어가고 true(서버).
	bool TryEscape();

	enum class EKnockdownPhase : uint8
	{
		None,
		Falling,
		Airborne, // 띄워져 떠 있음(LaunchMontage, AirLoopMontage). 착지 감시 타이머가 Landing 으로 전환한다.
		Slamming,
		Landing, // LandMontage. 끝나면 Hold.
		Hold,
		Countering, // 엎드린 채 반격 능력이 도는 중. 끝나면 Hold 로 돌아온다.
		GettingUp
	};

	bool IsAirbornePhase() const { return Phase == EKnockdownPhase::Airborne || Phase == EKnockdownPhase::Slamming; }

	// 다운 세트. HitReactConfig 가 없으면 null.
	const FVBKnockdownSet* GetKnockdownSet() const;

	// 몽타주 태스크 하나를 만들어 돌려준다(아직 켜지 않음). 종료 배선은 호출자가 하고 ReadyForActivation 도 호출자가 부른다.
	UAbilityTask_PlayMontageAndWait* MakeMontageTask(UAnimMontage* Montage);

	// 못 넘어지는 이유를 돌려준다. 넘어질 수 있으면 nullptr.
	const TCHAR* ResolveNoFallReason(const FVBKnockdownSet* Set, bool bFromImpact) const;
	// 이번 다운의 클립 묶음을 고르고, 띄움 길이면 true 를 돌려준다.
	bool PickDownClips(const FVBKnockdownSet& Set, bool bFromImpact, const FGameplayEventData* TriggerEventData);
	// 넘어뜨리는 타격 쪽으로 몸을 돌려 세운다.
	void TurnToFaceImpact(const FGameplayEventData& Event);
	// 다운 중 피격 이벤트 감시를 건다.
	void WatchDownHits();
	// 대기 클립이 표준 블렌드로 들어오면 경고한다.
	void WarnIfDownIdleBlendsIn() const;
	// 넘어지는 다운의 시작을 로그로 남긴다.
	void LogGoingDown(const FVBKnockdownSet& Set) const;
	// 다운 단계 상태를 지우고 공중 또는 넘어짐 흐름으로 들어간다.
	void BeginDownPath(const FVBKnockdownSet& Set, bool bLaunchPath, const FGameplayEventData* TriggerEventData);
	void BeginFalling();
	void BeginAirborne(const FVBKnockdownSet& Set);
	// 띄운 쪽에 떴다고 알린다(서버) - 공중 콤보가 쫓을 수 있게.
	void AnnounceLaunchTo(const AActor* Attacker);
	UFUNCTION()
	void OnAirSlamRequested(FGameplayEventData Payload);
	UFUNCTION()
	void OnAirHoldRequested(FGameplayEventData Payload);
	UFUNCTION()
	void OnAirReleaseRequested(FGameplayEventData Payload);
	bool IsValidAirRequest(const FGameplayEventData& Payload) const;
	// 넘어짐 데이터의 저작 문제를 자산마다 한 번 크게 알린다 - 다운이 서 있는 경직으로 내려앉을 때.
	void ReportKnockdownProblemsOnce() const;
	// 공중 피격 한 대: 체공 한도를 늘리고 위로 받친다.
	void HoldUpAfterAirHit(const FVBKnockdownSet& Set);
	void PlayAirLoop();
	void TickAirWatch();
	void BeginLanding();
	void BeginHold();
	void PlayDownIdle();
	// 기상을 튼다. 회피로 빠져나오면 빠른 기상 클립을 쓴다.
	void BeginGetUp(bool bEscaping = false);
	// 대기가 끝났고 끼어든 것(다운 피격, 반격)이 없으면 기상으로. 끼어든 것이 있으면 그것이 끝날 때 다시 묻는다.
	void ResumeAfterInterlude();

	// 전신 피격 반응을 재생하지 못하는 구간의 피격 반응. 지금 도는 몽타주를 멈추지 않고 가산 슬롯에 얹는다. 못 틀면 이유를 로그로 남긴다.
	void PlayFlinch(EVBHitDirection Direction, const TCHAR* Situation);

	// 지금 단계가 전신 피격 반응을 재생하지 못하는 구간이면 그 이름을, 재생할 수 있으면 nullptr 을 돌려준다. 넘어지는 중이면 클립 위치도 읽는다.
	const TCHAR* ResolveFlinchSituation(const FVBKnockdownSet& Set, float& OutFallPosition) const;

	// 떠 있는 동안의 피격 반응. 공중 피격 클립을 틀고 체공 한도를 늘린다.
	void PlayAirHit(const FVBKnockdownSet& Set, EVBHitDirection Direction);

	// 엎드린 동안의 전신 피격 반응. 넘어짐을 끊었으면 남은 시간을 대기에 더하고, 앞 반응을 교체한다.
	void PlayDownHit(const FVBKnockdownSet& Set, EVBHitDirection Direction, float FallPosition);

	void TickDownAttackWatch();
	bool TryStartDownAttack(const FVBKnockdownSet& Set, double Now);
	void OnAnyAbilityEnded(const FAbilityEndedData& EndedData);
	// 반격이 끝났다. Hold 로 돌아와 다음 할 일을 묻는다.
	void FinishCounter();
	void OnHoldExpired();

	UFUNCTION()
	void OnFallEnded();
	UFUNCTION()
	void OnAirLoopEnded();
	UFUNCTION()
	void OnLandEnded();
	UFUNCTION()
	void OnAirHitEnded();
	UFUNCTION()
	void OnDownIdleEnded();
	// 다운 피격이 제 끝에서 빠질 때(BlendOut)와 남에게 밀려날 때(Interrupted, Cancelled)를 가른다 - 앞은 뒤를 우리가 덮어야 하고, 뒤는 민 쪽이 덮는다.
	UFUNCTION()
	void OnDownHitEnded();
	UFUNCTION()
	void OnDownHitInterrupted();
	UFUNCTION()
	void OnGetUpEnded();
	UFUNCTION()
	void OnHitEventReceived(FGameplayEventData Payload);

	// 반격 몽타주의 BlendOut 시작 - 능력 종료 알림은 블렌드아웃이 끝난 뒤라 그 사이 서 있는 자세가 드러나므로 여기서 대기를 먼저 건다
	UFUNCTION()
	void OnAnyMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	EKnockdownPhase Phase = EKnockdownPhase::None;
	// 시작할 때의 스탠스를 기상까지 유지한다.
	EVBWeaponType ActiveStance = EVBWeaponType::None;

	// 서버 전용 래치들. 태그, GE 는 건 경우에만 떼려고 둔다 (공격 GA 의 GrantedSuperArmorTag 와 같은 이유).
	bool bDownedTagGranted = false;
	bool bHoldExpired = false;
	// 넘어짐이나 대기 위에 다운 피격이 도는 중이다. 두 단계에 걸쳐 있어 Phase 로 대신할 수 없다.
	bool bDownHitPlaying = false;

	FActiveGameplayEffectHandle VulnerabilityHandle;
	FTimerHandle HoldTimer;
	FTimerHandle AirTimer;

	FVBAirSuspension AirSuspension;
	FVBDownCounterWatch DownCounter;
	// 공중 피격 클립이 도는 중이면 체공 반복을 틀지 않는다. Airborne 안에서만 읽는다.
	bool bAirHitPlaying = false;
	// 이번 다운이 띄움에서 왔나 - 엎드림 시간과 기상 클립을 세트에 물을 때 전달한다.
	bool bArrivedByLaunch = false;
	// 엎드린 대기에 들어간 시각.
	double HoldStartTime = 0.0;

	// 이번 다운의 클립 묶음과 고르기 상태(직전 묶음 번호, 순번). 활성화 사이에 남는다(InstancedPerActor).
	UPROPERTY()
	FVBKnockdownClips ActiveClips;
	// 길(땅, 띄움)마다 직전 고른 묶음 - 두 목록은 길이가 달라 번호를 나누면 안 된다.
	int32 ClipPickIndex[2] = {INDEX_NONE, INDEX_NONE};
	int32 ClipPickSequence = 0;
	// 이번 띄우기를 시작한 공격자만 내려찍기 낙하를 요청할 수 있다.
	TWeakObjectPtr<const AActor> AirLauncher;

	/** 실제 바닥 접촉 한 번에만 사용할 충격 큐 */
	FGameplayTag PendingSlamImpactCue;
	void ClearSlamImpact();
	UFUNCTION()
	void OnSlamImpact(const FHitResult& Hit);
	UFUNCTION()
	void OnSlamReactionEnded();
	TWeakObjectPtr<UAbilityTask_PlayMontageAndWait> SlamReactionTask;

	// 재생 중인 다운 피격 태스크 - 갈아 끼울 때 앞 태스크의 종료 알림을 떼어 새 반응이 대기에 덮이지 않게 한다 (소유는 능력이라 약한 참조)
	TWeakObjectPtr<UAbilityTask_PlayMontageAndWait> DownHitTask;

	// 기상이 끝난 시각 + CooldownSeconds. 이 전의 파탄은 서 있는 경직. InstancedPerActor 라 활성화 사이에 남는다.
	double CooldownUntil = 0.0;

	// 넘어짐을 다운 피격으로 끊었을 때 남아 있던 클립 시간(초). BeginHold 가 대기 시간에 더한다 - 전체 다운 길이를 지키려고.
	float FallTimeCutShort = 0.0f;
};

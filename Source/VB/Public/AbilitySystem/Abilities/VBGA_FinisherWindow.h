// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_FinisherWindow.generated.h"

class UVBFinisherWindowConfig;
class UContextualAnimSceneActorComponent;

/**
 * 처형 창 - 적이 잠시 무력해지고 그동안에만 처형 키가 걸린다. State.Combat.FinisherWindow 의 단독 생산자다.
 * 여는 사건은 피해자의 설정이 고른다. 게이지로 연 창은 일반 타격의 피해, 반응을 받고, 체력 문턱으로 연 창은 처형 피해만 받는다. 게이지는 안 깎인다.
 * 닫히는 길(만료, 사망, 취소)은 전부 EndAbility 를 지나 손으로 붙인 창 태그를 거기서 뗀다.
 */
UCLASS(Abstract)
class VB_API UVBGA_FinisherWindow : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_FinisherWindow();

	// 설정이 기절 게이지로 열면 부여 때 State.Combat.PostureExecutable 을 붙인다(단독 생산자). 설정이 비면 오류를 낸다.
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	// 부여 때 붙인 태그를 뗀다.
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

#if WITH_EDITOR
	// 저작된 창 능력(BP)은 설정이 있어야 한다.
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	// 설정의 OpenOn 에 맞는 사건만 받는다. 판정만 하고 부작용이 없다 - 여기서 거절하면 재무장 쿨다운이 타지 않는다.
	virtual bool ShouldAbilityRespondToEvent(
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const override;

	// 열린 적이 있을 때만 창 태그를 뗀다.
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 창의 튜닝값. 비면 창을 열지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	TObjectPtr<UVBFinisherWindowConfig> FinisherWindowConfig;

private:
	// 실제 참가 상태를 읽어 준비 표시만 켜고 끈다.
	void RefreshWindowCue();
	void OnInteractionParticipantChanged(AActor* Participant);
	UFUNCTION()
	void OnLegacySceneChanged(UContextualAnimSceneActorComponent* Scene);
	FDelegateHandle InteractionStateHandle;
	TWeakObjectPtr<UContextualAnimSceneActorComponent> ObservedLegacyScene;
	bool bWindowCueActive = false;

	// 제한 시간 만료. 처형 장면이 도는 중이면 닫기를 미룬다.
	UFUNCTION()
	void OnWindowExpired();

	// 소유자가 죽었다. 시체에 창 태그가 남지 않게 닫는다.
	UFUNCTION()
	void OnOwnerDied();

	// 장면 때문에 만료를 미룬 적이 있는가. 미룸 로그를 한 번만 남긴다.
	bool bDeferredForScene = false;

	// 일어났는지 다시 본다. 일어났으면 창을 연다.
	UFUNCTION()
	void OnLyingRecheck();

	// 소유자가 누워 있거나 일어나는 중인가(Downed, GettingUp).
	bool IsOwnerLying() const;

	// 누운 대기를 시작한 시각(월드 초).
	double LyingWaitStartTime = 0.0;

	// 창 태그, 발광, 공격 취소, 정지, 대기 자세, 창 시간을 건다. 한 활성화에 한 번.
	void OpenWindow();

	// 창 태그를 붙였는가.
	bool bWindowOpen = false;

	// 체력 문턱 창의 피해 면역 태그를 붙였는가.
	bool bImmuneTagged = false;

	// 부여 때 기절 게이지 태그를 붙였는가.
	bool bPostureTagGranted = false;

	// 반응 감시가 살아 있는가. 감시를 거는 순간 오는 태그 없음 알림은 무시한다.
	bool bReactionWatchLive = false;

	// 대기(휘청) 자세를 튼다 - 창이 열릴 때와 창 동안 맞은 반응이 끝났을 때.
	void PlayWaitPose();
	UFUNCTION()
	void OnHitReactionEnded();
};

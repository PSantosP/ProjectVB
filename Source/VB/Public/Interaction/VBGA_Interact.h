// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "Interaction/VBInteractableConfig.h"
#include "VBGA_Interact.generated.h"

class UVBInteractableComponent;

/**
 * 환경 상호작용(문, 레버)의 판정을 담당한다. 서버가 대상을 고르고 탭은 즉시, 홀드는 HoldDuration 뒤에 NotifyInteracted 를 부른다.
 * 캐릭터 동작이 있으면 정렬 위치로 옮긴 뒤 몽타주를 틀고 CommitTime 에 완료한다. 홀드는 입력을 떼면 취소된다.
 * 칼 수납은 무기 상태 컴포넌트의 임시 수납을 가져다 쓰고 끝날 때 되돌린다(사망 제외). [MP 구멍] 캐릭터 동작은 서버에서만 돈다.
 */
UCLASS()
class VB_API UVBGA_Interact : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_Interact();
	// 탭도 정렬/몽타주 동안 활성이다. 입력 해제로 취소할 수 있는 것은 홀드뿐이다.
	virtual bool ShouldCancelOnInputRelease() const override { return bHoldInteraction; }
	void HandleRepeatPress();

protected:
	// 켜진 채 다시 눌렸다(UVBAbilitySystemComponent::AbilityInputTagPressed -> AbilitySpecInputPressed). 반복 상호작용을 요청한다.
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// 대상 모드별 시작(서버). 세 함수 모두 스스로 끝내거나 동작, 홀드를 이어 간다.
	void BeginPush(UVBInteractableComponent* Target);
	void BeginTap(UVBInteractableComponent* Target, AActor* Avatar);
	void BeginHold(UVBInteractableComponent* Target);

	// 지금 활성화를 끝낸다.
	void FinishInteraction(bool bWasCancelled);

	// 서버에서 홀드 시간이 찼다.
	void HandleHoldCompleted();

	// 캐릭터 동작 분기(서버). 정렬 위치로 옮기고 -> 몽타주 시작 -> 접촉 시점에 상태 확정.
	void StartMotion(UVBInteractableComponent* Target, const struct FVBInteractionMotion& Motion);

	UFUNCTION()
	void HandleAlignFinished();

	UFUNCTION()
	void HandleMotionMontageEnded();

	// 발 접촉 전에 취소되면 상태를 바꾸지 않는다. 확정 후 취소되어도 열린 문은 유지한다.
	void CommitMotion();

	// 칼 수납을 걸 조건이 맞으면 건다. 홀드와 캐릭터 동작 둘이 부른다.
	void SheatheIfWanted(const class UVBInteractableConfig* Config);

	// 대상의 조건 칸에 걸린 시도(서버): 거절 효과 + 소유 클라이언트에 거절 글. 시작 판정과 완료 거절이 같은 경로를 쓴다.
	void Refuse(UVBInteractableComponent* Target, AActor* Avatar);

	// 캐릭터 동작 분기가 도착 뒤 틀 몽타주. 옮기는 동안 보관한다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> PendingMontage;

	// 캐릭터 동작의 정렬 위치(캡슐 중심). StartMotion 이 한 번 정한다 - 다가온 방향 기준은 다시 재면 위치가 바뀐다.
	FVector AlignDestination = FVector::ZeroVector;

	// 서버: 시작할 때 고른 대상. 홀드 완료 때 다시 고르지 않는다. 도중에 파괴될 수 있어 약참조.
	TWeakObjectPtr<UVBInteractableComponent> ActiveTarget;

	FTimerHandle HoldTimerHandle;
	FTimerHandle MotionCommitTimerHandle;
	bool bMotionCommitted = false;

	// 캐릭터 동작 동안 이동 입력을 막는다(bCancelOnMove 가 꺼진 줍기만). 확정되면 풀고, 그 뒤 이동하면 몽타주를 블렌드해 끝낸다.
	// [MP 구멍] 서버의 PC 에만 걸린다.
	void LockMoveInput();
	void UnlockMoveInput();
	void WatchMoveCancel();
	void CheckMoveBeforeCommit();
	void CheckMoveAfterCommit();
	// bIncludeRawStick: 원시 스틱 입력도 센다(인터랙터가 캡슐 방향을 제어하는 동안 필요).
	bool HasMoveInput(bool bIncludeRawStick) const;
	FVBMoveCancelGate MoveCancelGate;
	FTimerHandle PreCommitMoveTimerHandle;
	TWeakObjectPtr<AController> MoveLockedController;
	FTimerHandle PostCommitMoveTimerHandle;
	bool bTargetInitiallyActivated = false;
	// E 재입력으로 이번 동작(열기든 닫기든)을 빠르게 하라고 했나. 배속은 UVBInteractableConfig::ResolveFastRate 하나가 고른다.
	bool bFastRequested = false;

	// 이번 상호작용이 칼을 임시 수납했는가. 맨손이었거나 트래버설이 수납해 둔 경우 돌려놓으면 안 된다.
	bool bSheathedWeapon = false;
	bool bHoldInteraction = false;
};

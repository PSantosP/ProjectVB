// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact (PlayingImpact)
#include "Data/VBHitReactConfig.h"     // FVBHitReactPick (ClipPick)
#include "VBGA_HitReact.generated.h"

class UVBHitReactConfig;
class UAnimMontage;
class UCharacterMovementComponent;

/**
 * 피격 반응 어빌리티. 피격자 ASC 에서 Event.Combat.Hit 로 활성화되어 반응 몽타주를 재생하고, 그 몽타주가 공격을 끊는다.
 * 큐가 아니라 GA 인 이유: 큐는 데디케이티드 서버에서 돌지 않아 서버에서 공격이 안 끊긴다.
 */
UCLASS()
class VB_API UVBGA_HitReact : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_HitReact();

	// 클립 수평 이동 배율. 1 ~ MaxScale. 클립 거리 1cm 미만이거나 원하는 거리가 0 이하면 1.
	static float ComputeRootMotionPushScale(float DesiredCm, float ClipCm, float MaxScale);

protected:
	// 히트리액트 데이터(피격자 소유). 능력 세트로 부여되므로 설정이 피격자를 따라온다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UVBHitReactConfig> HitReactConfig;

	// 베이스가 ActivateAbility 를 final 로 잠그므로 여기를 오버라이드한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 실효 등급이 '없음' 이면 켜지지 않는다. 켜진 뒤 끝내면 HitReacting 태그가 한 프레임 서 버린다.
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayEventData* Payload) const override;

	// 밀림 방향 보정을 푸는 유일한 자리. 모든 종료 경로가 여기를 지난다.
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	// 몽타주 종료 4경로 공통 종료.
	UFUNCTION()
	void OnHitReactEnded();

	// 반응 클립의 루트모션 수평 이동을 밀린 방향으로 돌리고 PushDistanceCm 까지 늘린다. 캐릭터는 돌리지 않는다. 걸었으면 true.
	bool BeginRootMotionRedirect(UAnimMontage* Montage, float PushYaw, float PushDistanceCm);
	void EndRootMotionRedirect();
	// CMC ProcessRootMotionPostConvertToWorld 에 바인딩된다. 그 반응 몽타주가 루트모션 소유자일 때만 돌린다.
	FTransform RedirectRootMotion(const FTransform& WorldRootMotion, UCharacterMovementComponent* Movement, float DeltaSeconds);

	// 활성화 때 한 번 정한 보정 각. 매 프레임 재면 캐릭터가 움직이며 흔들린다.
	float RootMotionRedirectYaw = 0.0f;
	FQuat RootMotionRedirectRotation = FQuat::Identity; // 위 각의 회전 캐시
	// 활성화 때 한 번 정한 수평 이동 배율(1 = 클립 그대로).
	float RootMotionPushScale = 1.0f;
	TWeakObjectPtr<UAnimMontage> RedirectedMontage;
	TWeakObjectPtr<UCharacterMovementComponent> RedirectedMovement;

	// 몽타주별 클립 전 구간 루트모션 캐시. 몽타주를 고쳤으면 PIE 를 다시 켜야 반영된다.
	TMap<TObjectKey<UAnimMontage>, FVector> ClipTranslationCache;

	// HeavySet 미저작 경고를 액터당 한 번만 낸다.
	bool bWarnedMissingHeavy = false;

	// 지금 재생 중인 반응 등급. 더 약한 타격은 거절해 약한 반응이 강한 반응을 끊지 않게 한다.
	EVBHitImpact PlayingImpact = EVBHitImpact::None;

	// 대체 클립 고르기 상태. 활성화마다 순번을 올린다.
	FVBHitReactPick ClipPick;
};
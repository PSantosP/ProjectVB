// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/VBAbilityActivationPolicy.h"
#include "VBGameplayAbility.generated.h"

class ACharacter;
class UVBAbilitySystemComponent;
class UVBWeaponStateComponent;

// 입력 분배가 능력을 활성화하기 전에 전달하는 값 - 어느 입력의 몇 번째 누름인가.
struct FVBActivationRequest
{
	FGameplayTag InputTag;
	int32 PressSerial = INDEX_NONE;
};

/**
 * VB 어빌리티 베이스. 모든 GA 는 이것을 상속한다.
 * ActivateAbility 를 잠가 CommitAbility 를 강제하고, 활성화 정책을 관리한다.
 */
UCLASS(Abstract)
class VB_API UVBGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGameplayAbility();

	// 엔진 태그 판정 뒤에 전투 규칙 표 판정을 더한다. 우리 ASC 가 아니거나 이름표가 비면 켜지 않는다.
	virtual bool DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	// 활성화 정책 (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ability")
	EVBAbilityActivationPolicy ActivationPolicy = EVBAbilityActivationPolicy::OnInputTriggered;

	// 이 Ability를 트리거하는 Input Tag (Enhanced Input 연동)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ability")
	FGameplayTag InputTag;

	EVBAbilityActivationPolicy GetActivationPolicy() const { return ActivationPolicy; }

	// 켜져 있는 동안 자기 InputTag 외에 받는 입력. 받은 누름은 다른 능력을 켜지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Input", meta=(Categories="Input"))
	FGameplayTagContainer ClaimedInputTags;

	UPROPERTY(EditDefaultsOnly, Category="VB|Input")
	bool bIsToggleAbility = false;

	// 켜진 채 입력을 놓으면 끝나는가. 런타임으로 답이 바뀌는 능력은 재정의한다.
	virtual bool ShouldCancelOnInputRelease() const { return bCancelOnInputRelease; }

	// 입력으로 켜지기 전에 서버가 정할 것을 이벤트 데이터에 담는다(서버 전용, 부작용 없음).
	// ExecuteAbility 에서 고르면 늦다 - 엔진이 서버 본문보다 먼저 클라 활성화를 보낸다.
	virtual bool PrepareActivation(const FVBActivationRequest& Request, FGameplayEventData& OutPayload) const { return false; }

	// 켜진 채 입력을 놓으면 끝나는가. 기본은 끝나지 않음, 홀드 능력(가드, 질주)만 켠다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Input")
	bool bCancelOnInputRelease = false;

	// 입력으로 눌렀는데 켜지지 못하면 플레이어 알림 사건(OnPlayerAbilityFailure)을 내는가. 끄면 로그 한 줄뿐이다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Ability|Failure")
	bool bShowFailureToPlayer = false;

protected:
	// 커스텀 ASC 헬퍼
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// AvatarActor를 캐릭터로 가져오는 헬퍼
	ACharacter* GetAvatarCharacter() const;

	// 아바타의 무기 상태 컴포넌트. 없으면 무기가 없는 아바타(적)다.
	UVBWeaponStateComponent* GetWeaponStateFromActorInfo() const;

	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) {}

	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	// 전역 히트스톱. RealDuration = 체감 시간(초), TimeDilation = 시간 배율(0.01 = 거의 정지).
	// 기본 인자를 두지 않는다 - 값의 홈은 UVBAttackConfig 하나다.
	void PlayHitStop(float RealDuration, float TimeDilation);
	// 전투원 한정 히트스톱. 피격자와 공격 설정을 큐에 실어 VBGC_HitStop 이 범위를 가른다.
	void PlayHitStop(float RealDuration, float TimeDilation, const TArray<AActor*>& Victims, const UObject* AttackConfig);

	// 이 Ability 에서 재생할 AnimMontage (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation")
	TObjectPtr<UAnimMontage> AnimMontage;

	// 몽타주 재생 속도 (BP에서 조절 가능)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation")
	float MontagePlayRate = 1.0f;

private:
	// CommitAbility 를 강제하려고 final 로 잠근다. 파생은 ExecuteAbility 를 오버라이드한다.
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override final;
};

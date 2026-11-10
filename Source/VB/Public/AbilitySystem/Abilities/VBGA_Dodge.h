// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBDodgeConfig.h"
#include "VBGA_Dodge.generated.h"

class ACharacter;
class UVBWeaponDefinition;

/**
 * 회피 어빌리티. 몽타주가 있으면 루트모션으로, 없으면 LaunchCharacter 로 움직인다(둘을 겹치면 2배 이동).
 * 무적(State.Combat.Dodging)은 몽타주 길이와 분리해 DodgeDuration 타이머로 푼다. 판정, 이동, 타이머는 서버.
 */
UCLASS(Abstract)
class VB_API UVBGA_Dodge : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_Dodge();

	// 로컬 방향(X=전방, Y=우측) -> 45도 칸 8방향 + 종전 4방향(순수).
	static FVBDodgeHeading HeadingFromLocal(const FVector2D& Local);
	// 질주 중이고 구르기 클립이 있으면 구르기, 아니면 스텝(순수).
	static UAnimMontage* PickStepOrRoll(UAnimMontage* Step, UAnimMontage* Roll, bool bSprinting)
	{
		return bSprinting && Roll ? Roll : Step;
	}

protected:
	UPROPERTY(EditDefaultsOnly, Category="VB|Dodge")
	TObjectPtr<UVBDodgeConfig> DodgeConfig;
	FTimerHandle DodgeTimerHandle;
	// 회피 커밋 타이머는 UVBCombatInputComponent 에 있다. EndAbility 가 이 GA 의 타이머를 모두 지워 태그가 남기 때문이다.

private:
	// 회피 실행: 이동을 걸고 DodgeTimerHandle 만료까지 무적 태그를 유지한다. 락온은 유지한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 이동 입력, 마지막 입력, 수평 속도, 뒤쪽 순서로 회피 방향(월드 단위 벡터)을 정한다.
	static FVector ResolveDodgeDirection(const ACharacter& Avatar);

	// 무기와 방향으로 스텝 또는 구르기 몽타주를 고른다. 없으면 nullptr.
	UAnimMontage* PickDodgeMontage(const ACharacter& Avatar, const UVBWeaponDefinition* Weapon, const FVBDodgeHeading& Heading,
		bool& bOutRoll) const;

	// 루트모션 몽타주를 틀고 무기가 정한 시각에 달리기로 잇는다.
	void PlayDodgeMontage(ACharacter& Avatar, UAnimMontage* Montage, const UVBWeaponDefinition* Weapon,
		const FVBDodgeHeading& Heading, bool bRoll, const FVector& Direction);

	// 서버만: 공격 입력 커밋을 걸고 무적창 타이머를 시작한다.
	void BeginServerDodgeWindow(const ACharacter& Avatar, const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo);
};

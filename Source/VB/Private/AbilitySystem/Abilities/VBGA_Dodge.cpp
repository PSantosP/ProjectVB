// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Dodge.h"
#include "Character/VBCombatInputComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h" // SetLooseGameplayTagCount - 회피 커밋 태그
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBSlideMovementComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"   // 앉은 채 회피 첫 프레임 로그의 캡슐 반높이
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "VB/VB.h"
#include "Engine/World.h"
#include "TimerManager.h"   // VB_LOG

FVBDodgeHeading UVBGA_Dodge::HeadingFromLocal(const FVector2D& Local)
{
	FVBDodgeHeading Heading;
	if (Local.IsNearlyZero())
	{
		return Heading; // 정지 = 뒤(ResolveDirection 과 같은 폴백)
	}

	Heading.Cardinal = FMath::Abs(Local.X) >= FMath::Abs(Local.Y)
		? (Local.X >= 0.0f ? EVBDodgeDirection::Forward : EVBDodgeDirection::Backward)
		: (Local.Y >= 0.0f ? EVBDodgeDirection::Right : EVBDodgeDirection::Left);
	// 전방 기준 각도(오른쪽 +). 축 둘레 ±22.5도는 정방향, 그 사이는 대각선.
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	const float Abs = FMath::Abs(Deg);
	const bool bRight = Deg >= 0.0f;
	if (Abs <= 22.5f)
	{
		Heading.Eight = EVBDodgeDirection::Forward;
	}
	else if (Abs < 67.5f)
	{
		Heading.Eight = bRight ? EVBDodgeDirection::ForwardRight : EVBDodgeDirection::ForwardLeft;
	}
	else if (Abs <= 112.5f)
	{
		Heading.Eight = bRight ? EVBDodgeDirection::Right : EVBDodgeDirection::Left;
	}
	else if (Abs < 157.5f)
	{
		Heading.Eight = bRight ? EVBDodgeDirection::BackwardRight : EVBDodgeDirection::BackwardLeft;
	}
	else
	{
		Heading.Eight = EVBDodgeDirection::Backward;
	}

	return Heading;
}

UVBGA_Dodge::UVBGA_Dodge()
{
	InputTag = VBGameplayTags::Input_Dodge;
	// 타이머 핸들을 쓰려면 필수 (InstancingPolicy는 베이스에서도 InstancedPerActor라 중복이지만 명시 유지)
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// NetExecutionPolicy: 베이스의 ServerInitiated 상속.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Dodging);

	// 이 어빌리티의 정체를 말하는 이름표. 막고 끊는 규칙은 전투 규칙 표에 있다(빠지지 않게 BP 가 아니라 여기 둔다).
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Defense_Dodge));
}

void UVBGA_Dodge::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// ServerInitiated 라 클라에서도 실행된다. 서버 권위 작업은 bIsServer 로 나눈다.
	const bool bIsServer = ActorInfo && ActorInfo->IsNetAuthority();

	ACharacter* AvatarCharacter = GetAvatarCharacter();
	if (!AvatarCharacter || !DodgeConfig)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 회피는 플레이어 캐릭터만 받는다. 무기 상태가 없으면 캐릭터 조립이 깨진 것이라 맨손 회피로 넘어가지 않는다.
	UVBWeaponStateComponent* Weapons = GetWeaponStateFromActorInfo();
	if (!Weapons)
	{
		VB_LOG(Error, "%s: 아바타 %s 에 무기 상태 컴포넌트가 없다 - 회피를 끝낸다", *GetName(), *GetNameSafe(AvatarCharacter));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 앉은 채 시작한 회피의 첫 프레임 상태를 남긴다. 캡슐은 이동 틱이 세우므로 서기 몽타주와 겹쳤는지 여기서만 보인다.
	if (AvatarCharacter->bIsCrouched)
	{
		VB_LOG(Log, "회피: 앉은 채 시작 - 앉기 취소됨, 캡슐 반높이 %.1f(아직 앉은 높이), 이동 틱에 선다 [%s]",
			AvatarCharacter->GetCapsuleComponent() ? AvatarCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : -1.0f,
			bIsServer ? TEXT("서버") : TEXT("클라"));
	}

	Weapons->EnterCombat(); // 회피는 발도 없는 전투 자극이다.

	const FVector Direction = ResolveDodgeDirection(*AvatarCharacter);
	const FVector LocalDirection = AvatarCharacter->GetActorTransform().InverseTransformVectorNoScale(Direction);
	const FVBDodgeHeading Heading = HeadingFromLocal(FVector2D(LocalDirection.X, LocalDirection.Y));
	const UVBWeaponDefinition* Weapon = Weapons->GetSelectedDefinition();

	bool bRoll = false;
	if (UAnimMontage* DodgeMontage = PickDodgeMontage(*AvatarCharacter, Weapon, Heading, bRoll))
	{
		PlayDodgeMontage(*AvatarCharacter, DodgeMontage, Weapon, Heading, bRoll, Direction);
	}
	else if (bIsServer)
	{
		// 몽타주가 없으면 물리 추진. LaunchCharacter 는 서버 전용이다(클라가 부르면 보정 jitter).
		AvatarCharacter->LaunchCharacter(Direction * DodgeConfig->DodgeStrength + DodgeConfig->DodgeVerticalBoost, true, true);
	}

	if (bIsServer)
	{
		BeginServerDodgeWindow(*AvatarCharacter, Handle, ActorInfo, ActivationInfo);
	}
}

FVector UVBGA_Dodge::ResolveDodgeDirection(const ACharacter& Avatar)
{
	// Enhanced Input 발화 순서가 비결정적이라 Last 가 낡을 수 있어 Pending 을 먼저 본다.
	FVector Direction = Avatar.GetPendingMovementInputVector();
	if (Direction.IsNearlyZero())
	{
		Direction = Avatar.GetLastMovementInputVector();
	}

	if (Direction.IsNearlyZero())
	{
		FVector Velocity = Avatar.GetVelocity();
		Velocity.Z = 0.0f;
		if (!Velocity.IsNearlyZero())
		{
			Direction = Velocity.GetSafeNormal();
		}
	}

	if (Direction.IsNearlyZero())
	{
		Direction = -Avatar.GetActorForwardVector();
	}

	Direction.Normalize();
	return Direction;
}

UAnimMontage* UVBGA_Dodge::PickDodgeMontage(const ACharacter& Avatar, const UVBWeaponDefinition* Weapon,
	const FVBDodgeHeading& Heading, bool& bOutRoll) const
{
	UAnimMontage* StepMontage = DodgeConfig->ResolveDodgeMontage(Weapon ? Weapon->FindDodgeSet() : nullptr, Heading);

	// 구르기는 무기 자신의 구르기 세트에서만 찾는다. 통일 세트로 내려가면 스텝이 구르기 자리를 차지한다.
	const FVBDodgeMontageSet* RollSet = Weapon ? Weapon->FindRollSet() : nullptr;
	UAnimMontage* RollMontage = RollSet ? UVBDodgeConfig::PickFromSet(*RollSet, Heading) : nullptr;

	const UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(&Avatar);
	UAnimMontage* Picked = PickStepOrRoll(StepMontage, RollMontage, Intent && Intent->GetIsSprinting());
	bOutRoll = RollMontage && Picked == RollMontage;
	return Picked;
}

void UVBGA_Dodge::PlayDodgeMontage(ACharacter& Avatar, UAnimMontage* Montage, const UVBWeaponDefinition* Weapon,
	const FVBDodgeHeading& Heading, bool bRoll, const FVector& Direction)
{
	// 몽타주가 이동을 맡는다(LaunchCharacter 와 겹치면 2배 이동). 서버와 소유 클라 양쪽에서 로컬 재생한다.
	// bStopWhenAbilityEnds 는 false 다. 무적창이 끝나 GA 가 종료돼도 클립 루트모션은 끝까지 돌아야 한다.
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, DodgeConfig->DodgeMontagePlayRate, NAME_None, /*bStopWhenAbilityEnds=*/false, 1.0f, 0.0f, true);
	MontageTask->ReadyForActivation();

	if (!Weapon)
	{
		return;
	}

	// 회피 능력은 무적창과 함께 먼저 끝나므로 달리기로 잇는 시각은 이동 컴포넌트가 지켜본다.
	const FVBDodgeToRun& ToRun = Weapon->GetDodgeToRun(bRoll);
	if (UVBSlideMovementComponent* Movement = Cast<UVBSlideMovementComponent>(Avatar.GetCharacterMovement()))
	{
		Movement->ScheduleRunHandover(Montage, ToRun.Pick(Heading.Eight), ToRun.ExitTime, Direction, ToRun.MaxStickAngleDeg);
	}
}

void UVBGA_Dodge::BeginServerDodgeWindow(const ACharacter& Avatar, const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 공격 입력 컴포넌트가 서버 ASC 에 DodgeCommit 태그를 세우고 AttackCancelSeconds 뒤 푼다.
	if (DodgeConfig->AttackCancelSeconds > 0.0f)
	{
		if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(&Avatar))
		{
			CombatInput->BeginDodgeCommit(DodgeConfig->AttackCancelSeconds, DodgeConfig->AttackBufferSeconds);
		}
	}

	// 이 타이머가 무적창의 수명이다. EndAbility 는 클라로 복제된다.
	if (UWorld* World = GetWorld())
	{
		const FTimerDelegate EndDodge = FTimerDelegate::CreateWeakLambda(this, [this, Handle, ActorInfo, ActivationInfo]()
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		});
		World->GetTimerManager().SetTimer(DodgeTimerHandle, EndDodge, DodgeConfig->DodgeDuration, false);
	}
}

void UVBGA_Dodge::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DodgeTimerHandle);
	}

	// 취소로 끝났으면 커밋도 바로 풀고 저장된 공격 입력을 버린다. 정상 종료는 커밋을 그대로 둔다(커밋이 무적보다 길다).
	if (bWasCancelled && ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(GetAvatarActorFromActorInfo()))
		{
			CombatInput->CancelDodgeCommit();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

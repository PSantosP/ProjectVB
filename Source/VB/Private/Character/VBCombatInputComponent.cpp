// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBCombatInputComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBPawnExtensionComponent.h"
#include "Character/VBTargetLockPolicy.h"
#include "Character/VBWeaponStateComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"
#include "VB/VB.h"

UVBCombatInputComponent::UVBCombatInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // 서버 입구 RPC 의 자리
}

UVBCombatInputComponent* UVBCombatInputComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBCombatInputComponent>() : nullptr;
}

UVBAbilitySystemComponent* UVBCombatInputComponent::GetAbilitySystem() const
{
	const UVBPawnExtensionComponent* Extension = UVBPawnExtensionComponent::FindFor(GetOwner());
	return Extension ? Extension->GetAbilitySystemComponent() : nullptr;
}

void UVBCombatInputComponent::LocalAttackPressed(FVector2D RawMoveInput)
{
	ACharacter* Body = Cast<ACharacter>(GetOwner());
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!Body || !ASC)
	{
		return;
	}

	if (!Body->HasAuthority() && Body->bIsCrouched)
	{
		Body->UnCrouch();
	}

	// 이미 공격 중이면 콤보 입력 - 소유 클라는 섹션 점프를 몽타주 복제로 못 받으므로(OnRep_ReplicatedAnimMontage 가 로컬 조종 캐릭터를 건너뜀)
	//   자기 공격 능력에 콤보 사건을 먼저 보낸다. 서버는 RPC 입구에서 같은 사건을 따로 보낸다.
	if (!Body->HasAuthority() && UVBAbilitySystemStatics::IsAttacking(ASC))
	{
		FGameplayEventData EventData;
		EventData.EventTag = VBGameplayTags::Event_Combo_InputReceived;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Body, VBGameplayTags::Event_Combo_InputReceived, EventData);
	}
	// 누르는 순간의 이동 방향을 같이 보낸다 - 카메라가 나중에 돌아도 이 누름의 뜻은 그때의 월드 방향이다.
	const AController* Controller = Body->GetController();
	const float ControlYaw = Controller ? Controller->GetControlRotation().Yaw : Body->GetActorRotation().Yaw;
	ServerAttackPressed(ASC->BeginLocalPress(VBGameplayTags::Input_Attack_Light),
		VBTargetLockPolicy::WorldMoveIntent(RawMoveInput, ControlYaw));
}

void UVBCombatInputComponent::LocalAttackReleased()
{
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	const int32 Serial = ASC ? ASC->EndLocalPress(VBGameplayTags::Input_Attack_Light) : INDEX_NONE;
	if (Serial == INDEX_NONE)
	{
		return;
	}
	// 기계마다 켜진 능력에 놓음 통지는 한 번: 원격 소유 클라는 여기서, 서버(리슨 호스트 포함)는 RPC 입구에서.
	if (!GetOwner()->HasAuthority())
	{
		ASC->AbilityInputTagReleased(VBGameplayTags::Input_Attack_Light);
	}

	ServerAttackReleased(Serial);
}

void UVBCombatInputComponent::LocalGuardPressed()
{
	if (UVBAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ServerGuardPressed(ASC->BeginLocalPress(VBGameplayTags::Input_Combat_Guard));
	}
}

void UVBCombatInputComponent::LocalGuardReleased()
{
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	const int32 Serial = ASC ? ASC->EndLocalPress(VBGameplayTags::Input_Combat_Guard) : INDEX_NONE;
	if (Serial != INDEX_NONE)
	{
		ServerGuardReleased(Serial);
	}
}

FVector2D UVBCombatInputComponent::ConsumeAttackIntent()
{
	const FVector2D Intent = PendingIntent;
	PendingIntent = FVector2D::ZeroVector;
	return Intent;
}

bool UVBCombatInputComponent::AcceptsRequests() const
{
	const UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	return ASC && ASC->GetAvatarActor() == GetOwner() && !UVBAbilitySystemStatics::IsDead(ASC);
}

bool UVBCombatInputComponent::ServerAttackPressed_Validate(int32 PressSerial, FVector2D MoveIntent)
{
	return true; // 번호는 누름 기록이 거른다(이미 아는 번호는 무시)
}

void UVBCombatInputComponent::ServerAttackPressed_Implementation(int32 PressSerial, FVector2D MoveIntent)
{
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	if (AcceptsRequests() && ASC->AcceptPressRequest(VBGameplayTags::Input_Attack_Light, PressSerial))
	{
		// 받아들인 누름의 방향만 둔다. 틀린 값(NaN, 길이 1 초과)은 방향 없음 -공격 자체는 막지 않는다. 허용 넘침은 부동소수 오차뿐이다.
		constexpr float MaxSentIntentLength = 1.01f;
		PendingIntent = (!MoveIntent.ContainsNaN() && MoveIntent.Size() <= MaxSentIntentLength) ? MoveIntent : FVector2D::ZeroVector;
		DispatchPress(PressSerial);
	}
}

void UVBCombatInputComponent::ServerAttackReleased_Implementation(int32 PressSerial)
{
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	if (AcceptsRequests() && ASC->AcceptReleaseRequest(VBGameplayTags::Input_Attack_Light, PressSerial))
	{
		ASC->AbilityInputTagReleased(VBGameplayTags::Input_Attack_Light);
	}
}

bool UVBCombatInputComponent::ServerGuardPressed_Validate(int32 PressSerial)
{
	return true; // 번호는 누름 기록이 거른다
}

void UVBCombatInputComponent::ServerGuardPressed_Implementation(int32 PressSerial)
{
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	if (AcceptsRequests() && ASC->AcceptPressRequest(VBGameplayTags::Input_Combat_Guard, PressSerial))
	{
		DispatchToAbilities(VBGameplayTags::Input_Combat_Guard, PressSerial);
	}
}

void UVBCombatInputComponent::ServerGuardReleased_Implementation(int32 PressSerial)
{
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	if (AcceptsRequests() && ASC->AcceptReleaseRequest(VBGameplayTags::Input_Combat_Guard, PressSerial))
	{
		ASC->AbilityInputTagReleased(VBGameplayTags::Input_Combat_Guard);
	}
}

void UVBCombatInputComponent::DispatchToAbilities(const FGameplayTag& InputTag, int32 PressSerial)
{
	UVBHeroComponent* Hero = UVBHeroComponent::FindFor(GetOwner());
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!Hero || !ASC)
	{
		return;
	}
	// 공격, 가드는 손에 든 무기의 세트가 준다 - 칼집 상태면 같은 누름 안에서 먼저 꺼낸다(서버의 발도는 동기라 새 스펙이 이 누름을 받는다).
	//   지금 시작할 수 없는 누름(벽 매달림, 경직 등, 전투 규칙 표)이면 꺼내지 않는다 - DrawForPress 가 그 무기 능력의 태그 조건을 먼저 묻는다.
	if (UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(GetOwner()))
	{
		if (Weapons->DrawForPress(InputTag) == EVBPressDraw::NoOwnedWeapon)
		{
			// 맨손 공격으로 떨어뜨리지 않는다 - 프롤로그는 무기를 주워야 싸우고, 맨손(Fighter)도 가진 무기의 하나다.
			ASC->PresentPressFailure(VBGameplayTags::Ability_Fail_NoOwnedWeapon);
			return;
		}
	}

	Hero->DispatchAbilityInput(InputTag, PressSerial);
}

double UVBCombatInputComponent::PressAge(int32 PressSerial) const
{
	const UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	const FVBInputPress* Press = ASC ? ASC->FindPress(VBGameplayTags::Input_Attack_Light, PressSerial) : nullptr;
	return Press && GetWorld() ? GetWorld()->GetTimeSeconds() - Press->PressTime : -1.0;
}

void UVBCombatInputComponent::DispatchPress(int32 PressSerial)
{
	ACharacter* Body = Cast<ACharacter>(GetOwner());
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Body);
	if (!Body || !ASC || !Hero)
	{
		return;
	}

	// 회피 커밋 중: 경공격이 태그로 막혀 있으니 누름을 버리지 않고 저장한다 - 커밋이 풀리면 EndDodgeCommit 이 내보낸다.
	if (ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_DodgeCommit))
	{
		if (GetWorld()->GetTimerManager().IsTimerActive(DodgeCommitTimer))
		{
			PendingDodgePress = PressSerial;
			VB_LOG(Log, "[Combo] 회피 커밋 중 공격 입력 - 저장(누름 %d)", PressSerial);
			return;
		}
		// 타이머 없이 태그만 남았으면 배선 결함이다 - 공격을 영영 못 하게 두지 않고 태그를 걷고 진행하되 소리낸다.
		VB_LOG(Warning, "[Combo] DodgeCommit 태그가 타이머 없이 남아 있다 - 걷어내고 공격 진행 (배선 결함)");
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
	}

	if (Body->bIsCrouched || ASC->HasMatchingGameplayTag(VBGameplayTags::State_Locomotion_Crouching))
	{
		PendingStandPress = PressSerial;
		FGameplayTagContainer CrouchAbilityTags(VBGameplayTags::Ability_Locomotion_Crouch);
		ASC->CancelAbilities(&CrouchAbilityTags);
		Body->UnCrouch();
		// 일어서기가 비동기면 HandleStoodUp 이 내보내고, 동기로 끝났으면 이미 내보내 대기가 비었다 - 한 번만.
		if (Body->bIsCrouched || PendingStandPress == INDEX_NONE)
		{
			return;
		}

		PendingStandPress = INDEX_NONE;
	}

	DispatchToAbilities(VBGameplayTags::Input_Attack_Light, PressSerial);
}

void UVBCombatInputComponent::HandleStoodUp(float BufferSeconds)
{
	const ACharacter* Body = Cast<ACharacter>(GetOwner());
	if (!Body || !Body->HasAuthority() || PendingStandPress == INDEX_NONE)
	{
		return;
	}

	const int32 PressSerial = PendingStandPress;
	PendingStandPress = INDEX_NONE;
	const double Age = PressAge(PressSerial);
	if (Age >= 0.0 && Age <= BufferSeconds && !Body->bIsCrouched)
	{
		DispatchPress(PressSerial);
	}
}

void UVBCombatInputComponent::BeginDodgeCommit(float CancelSeconds, float BufferSeconds)
{
	if (!GetOwner()->HasAuthority() || CancelSeconds <= 0.0f)
	{
		return;
	}

	if (UVBAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 1);
	}

	GetWorld()->GetTimerManager().SetTimer(DodgeCommitTimer,
		FTimerDelegate::CreateUObject(this, &UVBCombatInputComponent::EndDodgeCommit, BufferSeconds), CancelSeconds, false);
}

void UVBCombatInputComponent::EndDodgeCommit(float BufferSeconds)
{
	if (UVBAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
	}

	if (PendingDodgePress == INDEX_NONE)
	{
		return;
	}

	const int32 PressSerial = PendingDodgePress;
	PendingDodgePress = INDEX_NONE;
	const double Age = PressAge(PressSerial);
	if (Age >= 0.0 && Age <= BufferSeconds)
	{
		// 회피 오프셋(VBGA_MeleeAttackBase)이 콤보 자리를 이어 준다.
		VB_LOG(Log, "[Combo] 회피 커밋 해제 - 저장 누름 %d(나이 %.2f초) 내보냄", PressSerial, Age);
		DispatchToAbilities(VBGameplayTags::Input_Attack_Light, PressSerial);
	}
	else
	{
		VB_LOG(Log, "[Combo] 회피 커밋 해제 - 저장 누름 %d 나이 %.2f초(기준 %.2f) - 버림", PressSerial, Age, BufferSeconds);
		PendingIntent = FVector2D::ZeroVector; // 버린 누름의 방향은 남기지 않는다
	}
}

void UVBCombatInputComponent::CancelDodgeCommit()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(DodgeCommitTimer);
	if (UVBAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
	}

	PendingDodgePress = INDEX_NONE;
	PendingIntent = FVector2D::ZeroVector;
}

void UVBCombatInputComponent::ResetForAvatarSession(UVBAbilitySystemComponent* SessionAbilitySystem)
{
	ClearAirRecovery();
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DodgeCommitTimer);
	}

	if (SessionAbilitySystem)
	{
		SessionAbilitySystem->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
	}

	PendingDodgePress = INDEX_NONE;
	PendingStandPress = INDEX_NONE;
	PendingIntent = FVector2D::ZeroVector;
}

void UVBCombatInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetForAvatarSession(nullptr); // 타이머, 대기만 정리 - 태그는 해제 경로가 그 회차의 ASC 에서 내린다
	Super::EndPlay(EndPlayReason);
}

void UVBCombatInputComponent::BeginAirRecovery()
{
	ACharacter* Body = Cast<ACharacter>(GetOwner());
	UVBAbilitySystemComponent* ASC = GetAbilitySystem();
	const UCharacterMovementComponent* Move = Body ? Body->GetCharacterMovement() : nullptr;
	if (!Body || !Body->HasAuthority() || !ASC || !Move || Move->IsMovingOnGround()
		|| UVBAbilitySystemStatics::IsDead(ASC))
	{
		return;
	}

	if (AirRecoveryAbilitySystem.Get() == ASC)
	{
		return;
	}

	ClearAirRecovery();
	AirRecoveryAbilitySystem = ASC;
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_AirRecovery, 1, EGameplayTagReplicationState::CountToOwner);
	Body->LandedDelegate.AddUniqueDynamic(this, &ThisClass::OnRecoveryLanded);
	Body->MovementModeChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnRecoveryMovementChanged);
	PendingDodgePress = INDEX_NONE;
	PendingStandPress = INDEX_NONE;
}

void UVBCombatInputComponent::OnRecoveryLanded(const FHitResult& Hit)
{
	ClearAirRecovery();
}

void UVBCombatInputComponent::OnRecoveryMovementChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	if (Character && Character->GetCharacterMovement()->IsMovingOnGround())
	{
		ClearAirRecovery();
	}
}

void UVBCombatInputComponent::ClearAirRecovery()
{
	UVBAbilitySystemComponent* ASC = AirRecoveryAbilitySystem.Get();
	AirRecoveryAbilitySystem.Reset();
	if (ACharacter* Body = Cast<ACharacter>(GetOwner()))
	{
		Body->LandedDelegate.RemoveDynamic(this, &ThisClass::OnRecoveryLanded);
		Body->MovementModeChangedDelegate.RemoveDynamic(this, &ThisClass::OnRecoveryMovementChanged);
	}

	if (ASC)
	{
		ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_AirRecovery, 1, EGameplayTagReplicationState::CountToOwner);
	}
}

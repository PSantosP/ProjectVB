// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBHeroComponent.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBCombatInputComponent.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponDefinition.h" // 숫자 키 칸의 무기 종류
#include "Data/VBMovementConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Input/VBInputConfig.h"
#include "Input/VBInputDeviceManager.h"
#include "Input/VBKeyBindingRules.h"
#include "InputActionValue.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Player/VBPlayerController.h"
#include "Player/VBWeaponWheelComponent.h"
#include "Traversal/VBTraversalComponent.h"
#include "VB/VB.h"

UVBHeroComponent::UVBHeroComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

UVBHeroComponent* UVBHeroComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBHeroComponent>() : nullptr;
}

FVector2D UVBHeroComponent::GetRawMoveInputFor(const AActor* Actor)
{
	const UVBHeroComponent* Hero = FindFor(Actor);
	return Hero ? Hero->RawMoveInput : FVector2D::ZeroVector;
}

bool UVBHeroComponent::FFrameInputTargets::IsComplete() const
{
	return LocomotionIntent.IsValid() && TargetLock.IsValid() && Interactor.IsValid() && SmartObject.IsValid() && Ragdoll.IsValid()
		&& Traversal.IsValid();
}

AVBCharacter* UVBHeroComponent::GetBody() const
{
	return GetOwner<AVBCharacter>();
}

void UVBHeroComponent::InitializePlayerInput(UInputComponent* PlayerInputComponent)
{
	RawMoveInput = FVector2D::ZeroVector;
	// 다시 묶으면 전 입력 회차에서 누르고 있던 필살기 조합은 놓음을 못 받는다 - 남기면 다음 앉기 놓음이 엉뚱한 필살기 놓음이 된다.
	bSkillChordHeld = false;
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AVBCharacter* Body = GetBody();
	if (!Input || !Body)
	{
		return;
	}

	CacheFrameTargets(Body);
	if (!InputConfig)
	{
		// Warning 인 이유: 배포 캐릭터의 빠진 설정은 시험, 자산 검사가 막는다. Error 면 설정 없는 C++ 캐릭터를 쓰는 시험이 모두 실패한다.
		VB_LOG(Warning, "%s: 영웅에 입력 설정(InputConfig)이 없다 - 이 몸은 키를 하나도 받지 않는다", *GetNameSafe(GetOwner()));
		return;
	}

	BindMovementActions(Input, Body);
	BindCombatActions(Input, Body);
	BindWeaponSlotActions(Input);
	BindUtilityActions(Input);
	BindAbilityActions(Input);
}

void UVBHeroComponent::CacheFrameTargets(AVBCharacter* Body)
{
	// 플레이어 캐릭터는 이 컴포넌트들을 늘 만든다(AVBCharacter 생성자). 하나라도 없으면 조립이 깨진 것 - 입력이 조용히 먹히지 않게 여기서 한 번 알린다.
	FrameTargets = { UVBLocomotionIntentComponent::FindFor(Body), UVBTargetLockComponent::FindFor(Body),
		UVBInteractorComponent::FindFor(Body),
		UVBGASPSmartObjectComponent::FindFor(Body), UVBPlayerRagdollComponent::FindFor(Body), UVBTraversalComponent::FindFor(Body) };
	if (!FrameTargets.IsComplete())
	{
		VB_LOG(Error, "%s: 이동 · 시점 입력을 받을 컴포넌트가 빠졌다(걸음 %d 락온 %d 상호작용 %d 스마트 오브젝트 %d 래그돌 %d 이동 기술 %d) - 빠진 쪽 입력은 무시된다",
			*GetNameSafe(Body), (int32)FrameTargets.LocomotionIntent.IsValid(), (int32)FrameTargets.TargetLock.IsValid(),
			(int32)FrameTargets.Interactor.IsValid(),
			(int32)FrameTargets.SmartObject.IsValid(), (int32)FrameTargets.Ragdoll.IsValid(), (int32)FrameTargets.Traversal.IsValid());
	}
}

const UInputAction* UVBHeroComponent::RequireNativeAction(const FGameplayTag& Tag) const
{
	const UInputAction* Action = InputConfig->FindNativeInputActionForTag(Tag);
	if (!Action)
	{
		VB_LOG(Warning, "%s: 입력 설정 %s 에 %s 줄이 없다 - 이 입력은 묶지 않는다", *GetNameSafe(GetOwner()), *GetNameSafe(InputConfig), *Tag.ToString());
	}

	return Action;
}

void UVBHeroComponent::BindMovementActions(UEnhancedInputComponent* Input, AVBCharacter* Body)
{
	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Move))
	{
		Input->BindAction(Action, ETriggerEvent::Triggered, this, &ThisClass::HandleMove);
		// 뗀 순간과 취소된 순간 둘 다 0 을 보낸다 - 매달림은 마지막 입력값을 상태로 들고 있어 0 을 받지 않으면 키를 떼도 계속 옆걸음한다.
		Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::HandleMoveCompleted);
		Input->BindAction(Action, ETriggerEvent::Canceled, this, &ThisClass::HandleMoveCompleted);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Look))
	{
		Input->BindAction(Action, ETriggerEvent::Triggered, this, &ThisClass::HandleLook);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Jump))
	{
		Input->BindAction(Action, ETriggerEvent::Started, Body, &AVBCharacter::JumpInputPressed);
		Input->BindAction(Action, ETriggerEvent::Completed, Body, &AVBCharacter::JumpInputReleased);
	}
}

void UVBHeroComponent::BindCombatActions(UEnhancedInputComponent* Input, AVBCharacter* Body)
{
	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Attack_Light))
	{
		Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleAttackPressed);
		// 놓음은 길게 판정의 사실이다. Canceled 도 놓음이다 - 입력 맥락이 빠져도 누르는 중 상태가 남지 않게.
		Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::HandleAttackReleased);
		Input->BindAction(Action, ETriggerEvent::Canceled, this, &ThisClass::HandleAttackReleased);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Crouch))
	{
		Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleCrouchPressed);
		Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::HandleCrouchReleased);
		Input->BindAction(Action, ETriggerEvent::Canceled, this, &ThisClass::HandleCrouchReleased);
	}

	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Body);
	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_WalkToggle); Action && Intent)
	{
		Input->BindAction(Action, ETriggerEvent::Started, Intent, &UVBLocomotionIntentComponent::ToggleWalk);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_LockOn))
	{
		Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleLockOnPressed);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_SwitchTarget))
	{
		Input->BindAction(Action, ETriggerEvent::Triggered, this, &ThisClass::HandleSwitchTarget);
		Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::HandleSwitchTargetReleased);
		Input->BindAction(Action, ETriggerEvent::Canceled, this, &ThisClass::HandleSwitchTargetReleased);
	}
}

void UVBHeroComponent::BindWeaponSlotActions(UEnhancedInputComponent* Input)
{
	// 숫자 키는 칸 번호만 전달한다. 어느 무기가 그 칸인지는 무기 정의(InputSlot)가 정한다.
	const FGameplayTag SlotTags[] = {VBGameplayTags::Input_WeaponSlot_1, VBGameplayTags::Input_WeaponSlot_2,
		VBGameplayTags::Input_WeaponSlot_3};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotTags); ++Index)
	{
		if (const UInputAction* Action = RequireNativeAction(SlotTags[Index]))
		{
			Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleWeaponSlot, Index + 1);
		}
	}
}

void UVBHeroComponent::BindUtilityActions(UEnhancedInputComponent* Input)
{
	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Interact))
	{
		// 홀드 대상은 떼면 취소된다.
		Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleInteractPressed);
		Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::HandleInteractReleased);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Ragdoll))
	{
		Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleRagdollPressed);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_Aim))
	{
		Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleAimPressed);
		Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::HandleAimReleased);
		// 취소(입력 모드 전환 등)는 토글이어도 내린다 - 손을 뗀 것과 강제 해제는 다르다.
		Input->BindAction(Action, ETriggerEvent::Canceled, this, &ThisClass::HandleAimCanceled);
	}

	if (const UInputAction* Action = RequireNativeAction(VBGameplayTags::Input_WeaponSummon))
	{
		// 이 입력 행동은 키보드 맥락에서 Tap 트리거를 단다 - Triggered 는 짧게 누르고 뗄 때 한 번 온다.
		Input->BindAction(Action, ETriggerEvent::Triggered, this, &ThisClass::HandleAbilityPressed,
			VBGameplayTags::Input_WeaponSummon.GetTag());
	}
}

void UVBHeroComponent::BindAbilityActions(UEnhancedInputComponent* Input)
{
	// Ability 줄: 누름은 Started 에 묶는다. Triggered 에 묶으면(Lyra) 누름 트리거 없는 행동이 누르고 있는 동안 매 프레임 와서
	//   회피가 다시 켜진다. 놓음은 Completed - 놓아서 끝날지는 능력의 bCancelOnInputRelease 가 정한다.
	for (const FVBInputAction& Row : InputConfig->AbilityInputActions)
	{
		if (!Row.InputAction || !Row.InputTag.IsValid())
		{
			continue; // 자산 검사(UVBInputConfig::FindProblems)가 이미 거절한 줄
		}

		Input->BindAction(Row.InputAction, ETriggerEvent::Started, this, &ThisClass::HandleAbilityPressed, Row.InputTag);
		Input->BindAction(Row.InputAction, ETriggerEvent::Completed, this, &ThisClass::HandleAbilityReleased, Row.InputTag);
	}
}

void UVBHeroComponent::EndPlayerInput()
{
	HandleMoveCompleted();
}

const UInputAction* UVBHeroComponent::FindInputAction(const FGameplayTag& InputTag) const
{
	if (!InputConfig)
	{
		return nullptr;
	}

	if (const UInputAction* Action = InputConfig->FindNativeInputActionForTag(InputTag))
	{
		return Action;
	}

	return InputConfig->FindAbilityInputActionForTag(InputTag);
}

bool UVBHeroComponent::ShouldBlockMoveForHitReact(const UAbilitySystemComponent* ASC)
{
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_HitReacting);
}

void UVBHeroComponent::HandleMove(const FInputActionValue& Value)
{
	AVBCharacter* Body = GetBody();
	if (!Body)
	{
		return;
	}

	const FVector2D MoveValue = Value.Get<FVector2D>();
	RawMoveInput = MoveValue; // 어느 경로로 가든 먼저 기록한다 - 아래 분기들이 일찍 돌아가도 입력 중이라는 사실은 남는다
	if (UVBLocomotionIntentComponent* Intent = FrameTargets.LocomotionIntent.Get())
	{
		Intent->ReportMoveInputMagnitude(MoveValue.Size(), Body->GetMovementConfig()->AnalogWalkRunThreshold); // 스틱을 조금만 기울이면 걷는다
	}

	if (UVBFinisherComponent::IsActorInScene(Body))
	{
		return;
	}
	// 맞고 휘청이는 동안은 이동을 막는다 - 없으면 입력이 캡슐만 밀고 다리는 맞은 자세로 서 있다. 회피는 자기 능력으로 이것을 끊는다.
	if (ShouldBlockMoveForHitReact(Body->GetAbilitySystemComponent()))
	{
		return;
	}

	// 지금 이동 입력을 처리할 담당을 고른다: 밀기 -> 상호작용 정렬 -> 래그돌 구르기 -> 매달림, 등반, 사다리 -> CMC.
	UVBInteractorComponent* Interactor = FrameTargets.Interactor.Get();
	if (Interactor && Interactor->GetPushable())
	{
		Interactor->SetPushInput(MoveValue.Y);
		return;
	}
	// 정렬/접촉 동작 중 입력으로 발이 미끄러지지 않게 한다. 끝난 뒤 누르고 있는 입력은 다음 틱에 다시 온다.
	if (Interactor && Interactor->OwnsCapsuleFacing())
	{
		return;
	}

	UVBGASPSmartObjectComponent* SmartObject = FrameTargets.SmartObject.Get();
	if (!MoveValue.IsNearlyZero() && SmartObject && SmartObject->IsInteracting())
	{
		SmartObject->RequestExit();
	}

	AController* Controller = Body->GetController();
	if (UVBPlayerRagdollComponent* Ragdoll = FrameTargets.Ragdoll.Get(); Ragdoll && Ragdoll->IsRagdollActive())
	{
		const FRotator Yaw(0, Controller ? Controller->GetControlRotation().Yaw : Body->GetActorRotation().Yaw, 0);
		Ragdoll->SetRollInput(Yaw.RotateVector(FVector(MoveValue.Y, MoveValue.X, 0)));
		return;
	}
	// 벽에 붙은 동안은 이동 기술이 입력을 처리한다. CMC 에 전달하면 Flying 가속이 붙어 정해진 자리에서 밀려난다.
	if (UVBTraversalComponent* Traversal = FrameTargets.Traversal.Get(); Traversal && Traversal->TryConsumeMoveInput(MoveValue))
	{
		return;
	}

	if (Controller)
	{
		const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		Body->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MoveValue.Y);
		Body->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MoveValue.X);
	}
}

void UVBHeroComponent::HandleMoveCompleted()
{
	RawMoveInput = FVector2D::ZeroVector;
	AVBCharacter* Body = GetBody();
	if (!Body)
	{
		return;
	}

	if (UVBLocomotionIntentComponent* Intent = FrameTargets.LocomotionIntent.Get())
	{
		Intent->ReportMoveInputMagnitude(0.f, Body->GetMovementConfig()->AnalogWalkRunThreshold); // 손을 뗐다 - 아날로그 걷기 이유가 풀린다
	}

	if (UVBInteractorComponent* Interactor = FrameTargets.Interactor.Get())
	{
		Interactor->SetPushInput(0.0f);
	}

	if (UVBPlayerRagdollComponent* Ragdoll = FrameTargets.Ragdoll.Get(); Ragdoll && Ragdoll->IsRagdollActive())
	{
		Ragdoll->SetRollInput(FVector::ZeroVector);
		return;
	}
	// 지상 이동은 입력이 멈추면 CMC 가 알아서 멎는다. 마지막 값을 상태로 드는 벽 위 이동만 0 을 받아야 한다.
	if (UVBTraversalComponent* Traversal = FrameTargets.Traversal.Get())
	{
		Traversal->TryConsumeMoveInput(FVector2D::ZeroVector);
	}
}

void UVBHeroComponent::HandleLook(const FInputActionValue& Value)
{
	AVBCharacter* Body = GetBody();
	if (!Body)
	{
		return;
	}

	if (const AVBPlayerController* Player = Cast<AVBPlayerController>(Body->GetController()))
	{
		if (Player->GetWeaponWheel() && Player->GetWeaponWheel()->IsOpen())
		{
			return;
		}
	}
	// 감도, Y 반전, 패드 데드존은 이미 걸려 온 값이다 - 장치마다 IMC 의 보정기(UVBInputModifier_LookCalibration,
	//   _StickDeadZone)가 건다. 여기서 다시 곱하면 두 번 걸린다(VB.Unit.Input.Calibration.ExactlyOnce).
	const FVector2D LookValue = Value.Get<FVector2D>();
	UVBTargetLockComponent* TargetLock = FrameTargets.TargetLock.Get();
	if (TargetLock)
	{
		TargetLock->NotifyManualCameraInput(LookValue.X, LookValue.Y);
	}

	if (TargetLock && TargetLock->CanTrackCamera())
	{
		// 패드 X 는 대상 전환 행동만 쓴다. 마우스는 대상을 바꾸지 않고 구도만 민다.
		const UVBInputDeviceManager* Devices = UVBInputDeviceManager::FindFor(Body);
		TargetLock->AddLookInput(LookValue.X, LookValue.Y, Devices && Devices->IsUsingGamepad());
		return;
	}

	Body->AddControllerYawInput(LookValue.X);
	Body->AddControllerPitchInput(LookValue.Y);
}

void UVBHeroComponent::HandleAttackPressed()
{
	if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(GetOwner()))
	{
		CombatInput->LocalAttackPressed(RawMoveInput);
	}
}

void UVBHeroComponent::HandleAttackReleased()
{
	if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(GetOwner()))
	{
		CombatInput->LocalAttackReleased();
	}
}

void UVBHeroComponent::HandleInteractPressed()
{
	AVBCharacter* Body = GetBody();
	if (!Body)
	{
		return;
	}
	// 환경 상호작용이 캐릭터를 맡고 있으면 그 상호작용의 입력이다. 아니면 스마트 오브젝트가 먼저 나가기, 들어가기를 받는다.
	UVBInteractorComponent* Interactor = UVBInteractorComponent::FindFor(Body);
	UVBGASPSmartObjectComponent* SmartObject = UVBGASPSmartObjectComponent::FindFor(Body);
	if (!(Interactor && Interactor->OwnsCapsuleFacing()))
	{
		if (SmartObject && SmartObject->IsInteracting())
		{
			SmartObject->RequestExit();
			return;
		}

		if ((!Interactor || !Interactor->FindBestInteractable()) && SmartObject && SmartObject->TryInteract())
		{
			return;
		}
	}

	DispatchAbilityInput(VBGameplayTags::Input_Interact);
}

void UVBHeroComponent::HandleInteractReleased()
{
	// 홀드만 취소한다. 탭의 정렬, 몽타주는 키를 뗀 뒤에도 이어진다.
	DispatchAbilityInputReleased(VBGameplayTags::Input_Interact);
}

void UVBHeroComponent::HandleCrouchPressed()
{
	AVBCharacter* Body = GetBody();
	if (!Body)
	{
		return;
	}

	bSkillChordHeld = IsSkillChordPress(*Body);
	if (bSkillChordHeld)
	{
		DispatchAbilityInput(VBGameplayTags::Input_CinematicSkill);
		return;
	}

	Body->CrouchInputPressed();
}

bool UVBHeroComponent::IsSkillChordPress(const AVBCharacter& Body) const
{
	// 질주 버튼을 지금 누르고 있나 - 질주 상태가 아니라 버튼이다(서서 L3 를 눌러도 질주는 안 된다).
	const APlayerController* PC = Cast<APlayerController>(Body.GetController());
	const UEnhancedPlayerInput* PlayerInput = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
	const UInputAction* SprintAction = FindInputAction(VBGameplayTags::Input_Sprint);
	const bool bSprintHeld = PlayerInput && SprintAction && PlayerInput->GetActionValue(SprintAction).Get<bool>();
	const UVBInputDeviceManager* Device = UVBInputDeviceManager::FindFor(&Body);
	// 지금 걸린 키 묶음에서 읽는다 - 플레이어가 옵션에서 묶은 키도 여기 들어 있다.
	bool bPadSkillKeyBound = false;
	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = PC
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	if (const UInputAction* SkillAction = FindInputAction(VBGameplayTags::Input_CinematicSkill); InputSubsystem && SkillAction)
	{
		for (const FKey& Key : InputSubsystem->QueryKeysMappedToAction(SkillAction))
		{
			bPadSkillKeyBound |= VBKeyBinding::DeviceOf(Key) == EVBKeyDevice::Gamepad;
		}
	}

	return VBKeyBinding::IsSkillChordPress(Device && Device->IsUsingGamepad(), bSprintHeld, !RawMoveInput.IsNearlyZero(), bPadSkillKeyBound);
}

void UVBHeroComponent::HandleCrouchReleased()
{
	// 앉기는 누름만 쓴다(토글) - 놓음은 필살기 조합의 짝일 때만 보낸다.
	if (bSkillChordHeld)
	{
		bSkillChordHeld = false;
		DispatchAbilityInputReleased(VBGameplayTags::Input_CinematicSkill);
	}
}

void UVBHeroComponent::HandleLockOnPressed()
{
	if (UVBTargetLockComponent* TargetLock = UVBTargetLockComponent::FindFor(GetOwner()))
	{
		TargetLock->ToggleLockOn();
	}
}

void UVBHeroComponent::HandleSwitchTarget(const FInputActionValue& Value)
{
	UVBTargetLockComponent* TargetLock = UVBTargetLockComponent::FindFor(GetOwner());
	if (!TargetLock)
	{
		return;
	}

	const UVBInputDeviceManager* Devices = UVBInputDeviceManager::FindFor(GetOwner());
	TargetLock->HandleSwitchInput(Value.Get<float>(), Devices && Devices->IsUsingGamepad());
}

void UVBHeroComponent::HandleSwitchTargetReleased()
{
	if (UVBTargetLockComponent* TargetLock = UVBTargetLockComponent::FindFor(GetOwner()))
	{
		TargetLock->ResetSwitchInput();
	}
}

void UVBHeroComponent::HandleWeaponSlot(int32 Slot)
{
	AVBCharacter* Body = GetBody();
	// 처형을 거는 중에는 물러난다 - 여기서 몽타주가 나가면 처형 애니가 끊겨 플러그인이 장면을 통째로 끝낸다.
	//   능력 입력은 UVBGA_Execute 의 차단 목록이 막지만, 이 길은 능력을 거치지 않고 무기 상태를 직접 부르므로 여기서 묻는다.
	if (!Body || UVBAbilitySystemStatics::IsActorFinishing(Body))
	{
		return;
	}

	UVBWeaponStateComponent* Weapon = UVBWeaponStateComponent::FindFor(Body);
	if (!Weapon)
	{
		return;
	}

	const UVBWeaponDefinition* OnSlot = Weapon->FindDefinitionForSlot(Slot);
	if (!OnSlot)
	{
		VB_LOG(Verbose, "숫자 키 %d 에 놓인 무기가 없다", Slot);
		return;
	}

	const EVBWeaponType RequestedType = OnSlot->GetWeaponType();
	if (Weapon->GetCurrentWeaponType() == RequestedType && Weapon->IsWeaponSummoned())
	{
		Weapon->SheatheWeapon();
	}
	else
	{
		Weapon->SummonWeapon(RequestedType);
	}
}

void UVBHeroComponent::HandleRagdollPressed()
{
	if (UVBPlayerRagdollComponent* Ragdoll = UVBPlayerRagdollComponent::FindFor(GetOwner()))
	{
		Ragdoll->RequestRagdoll();
	}
}

void UVBHeroComponent::HandleAimPressed()
{
	if (UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(GetOwner()))
	{
		Intent->SetWantsToAim(IsAimToggle() ? !Intent->WantsToAim() : true);
	}
}

void UVBHeroComponent::HandleAimReleased()
{
	if (UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(GetOwner()); Intent && !IsAimToggle())
	{
		Intent->SetWantsToAim(false);
	}
}

void UVBHeroComponent::HandleAimCanceled()
{
	if (UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(GetOwner()))
	{
		Intent->SetWantsToAim(false);
	}
}

bool UVBHeroComponent::IsAimToggle() const
{
	// 토글 판정의 집은 ASC 하나다(능력 입력과 같은 설정 규칙).
	const AVBCharacter* Body = GetBody();
	const UVBAbilitySystemComponent* ASC = Body ? Cast<UVBAbilitySystemComponent>(Body->GetAbilitySystemComponent()) : nullptr;
	return ASC && ASC->IsToggleInput(VBGameplayTags::Input_Aim, false);
}

void UVBHeroComponent::HandleAbilityPressed(FGameplayTag InputTag)
{
	// 가드는 공격처럼 서버 누름 길로 간다 - 칼집 상태면 서버가 꺼낸 뒤 같은 누름으로 켠다(소유 클라에는 그때 가드 스펙이 아직 없다).
	if (InputTag.MatchesTagExact(VBGameplayTags::Input_Combat_Guard))
	{
		if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(GetOwner()))
		{
			CombatInput->LocalGuardPressed();
		}

		return;
	}

	DispatchAbilityInput(InputTag);
}

void UVBHeroComponent::HandleAbilityReleased(FGameplayTag InputTag)
{
	if (InputTag.MatchesTagExact(VBGameplayTags::Input_Combat_Guard))
	{
		if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(GetOwner()))
		{
			CombatInput->LocalGuardReleased();
		}

		return;
	}

	DispatchAbilityInputReleased(InputTag);
}

void UVBHeroComponent::DispatchAbilityInput(FGameplayTag InputTag, int32 PressSerial)
{
	AVBCharacter* Body = GetBody();
	if (!Body)
	{
		return;
	}
	// 트래버설 중에 어떤 능력이 막히는지는 전투 규칙 표의 State.Traversal 줄이 정한다.
	if (UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(Body->GetAbilitySystemComponent()))
	{
		// 로컬 분배 길 - 가져간 능력이 있으면 ASC 가 서버에도 보낸다(경공격, 가드는 전투 입력 RPC 로 따로 간다).
		ASC->AbilityInputTagPressed(InputTag, PressSerial, /*bForwardClaim=*/true);
	}
}

void UVBHeroComponent::DispatchAbilityInputReleased(FGameplayTag InputTag)
{
	const AVBCharacter* Body = GetBody();
	if (UVBAbilitySystemComponent* ASC = Body ? Cast<UVBAbilitySystemComponent>(Body->GetAbilitySystemComponent()) : nullptr)
	{
		ASC->AbilityInputTagReleased(InputTag, /*bForwardClaim=*/true);
	}
}

void UVBHeroComponent::RefreshActiveCamera()
{
	AVBCharacter* Body = GetBody();
	UGameplayCameraComponent* GameplayCamera = Body ? Body->GetGameplayCamera() : nullptr;
	if (!GameplayCamera)
	{
		return;
	}

	APlayerController* Player = Cast<APlayerController>(Body->GetController());
	const bool bLocalPlayer = Player && Player->IsLocalController();
	if (bLocalPlayer && IsUsingGameplayCamera())
	{
		if (!GameplayCamera->IsActive())
		{
			GameplayCamera->ActivateCameraForPlayerController(Player, true);
		}
	}
	else
	{
		if (bLocalPlayer)
		{
			// Warning 인 이유: 배포 캐릭터의 빠진 자산은 시험이 막는다. Error 면 자산 없는 C++ 캐릭터를 쓰는 시험이 모두 실패한다.
			VB_LOG(Warning, "%s: GameplayCamera 에 카메라 자산이 없다 - 이 몸에는 켤 카메라가 없다", *GetNameSafe(Body));
		}

		if (UVBEnvironmentCameraComponent* Environment = UVBEnvironmentCameraComponent::FindFor(Body))
		{
			Environment->ReleaseCollisionOwnership();
		}

		if (GameplayCamera->IsActive())
		{
			GameplayCamera->Deactivate();
		}
	}
}

bool UVBHeroComponent::IsUsingGameplayCamera() const
{
	const AVBCharacter* Body = GetBody();
	const UGameplayCameraComponent* GameplayCamera = Body ? Body->GetGameplayCamera() : nullptr;
	return GameplayCamera && GameplayCamera->CameraReference.IsValid();
}


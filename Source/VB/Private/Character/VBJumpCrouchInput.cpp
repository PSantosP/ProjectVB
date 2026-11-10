// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBJumpCrouchInput.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBCharacter.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Character/VBSlideMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Traversal/VBTraversalComponent.h"
#include "VB/VB.h"

void FVBJumpCrouchInput::JumpPressed(const FParts& Parts)
{
	AVBCharacter& Body = *Parts.Body;
	// 점프가 되지 않는 길마다 이유를 한 줄 남긴다.
	const auto Refuse = [](const TCHAR* Reason) { VB_LOG(Log, "[Jump] 점프 누름 - 점프 안 함: %s", Reason); };
	VB_LOG(Log, "[Jump] 점프 누름 받음"); // 이 줄이 없으면 누름이 입력(매핑, UI 라우터)에서 사라진 것이다
	if (Parts.Interactor && Parts.Interactor->OwnsCapsuleFacing())
	{
		Refuse(TEXT("상호작용 정렬 중"));
		return;
	}

	if (Parts.SmartObject && Parts.SmartObject->IsInteracting())
	{
		Refuse(TEXT("스마트 오브젝트 상호작용 중"));
		return;
	}

	if (Parts.PlayerRagdoll && Parts.PlayerRagdoll->IsRagdollActive())
	{
		Refuse(TEXT("래그돌 - 일어서기 요청으로 씀"));
		Parts.PlayerRagdoll->RequestGetUp();
		return;
	}
	// 처형 중에는 물러난다 - 여기서 몽타주가 나가면 처형 장면이 통째로 끝난다. 능력 밖 경로라 스스로 묻는다.
	if (UVBAbilitySystemStatics::IsActorFinishing(&Body))
	{
		Refuse(TEXT("처형 중"));
		return;
	}

	bool bJumpedOutOfSlide = false;
	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(Body.GetCharacterMovement()))
	{
		bJumpedOutOfSlide = SlideCMC->IsSliding();
		SlideCMC->CancelSlide();
	}

	// 매달린 채 점프 = 올라서기. 홀드 플래그는 세우지 않는다(남으면 같은 렛지를 다시 문다).
	if (Parts.Traversal && Parts.Traversal->IsHanging())
	{
		Refuse(TEXT("매달림 - 올라서기 요청으로 씀"));
		Parts.Traversal->RequestHangClimbUp();
		return;
	}
	// 등반 중 점프는 아직 할 일이 없다 - 일반 점프가 캡슐을 벽에서 튕겨 내므로 끊는다.
	if (Parts.Traversal && Parts.Traversal->IsClimbing())
	{
		Refuse(TEXT("등반 중"));
		return;
	}

	// 공중 맨틀 홀드 - Tick 의 공중 체크가 이어받는다.
	bJumpInputHeld = true;
	SlideJumpRequest.OnPress();

	// 지상에서만 트래버설, 점프(GASP 와 같은 게이트) - 공중 재입력은 무시한다.
	const UCharacterMovementComponent* CMC = Body.GetCharacterMovement();
	if (!CMC || !CMC->IsMovingOnGround())
	{
		VB_LOG(Log, "[Jump] 점프 누름 - 점프 안 함: 땅 위가 아님(이동 모드 %d)", CMC ? static_cast<int32>(CMC->MovementMode.GetValue()) : -1);
		return;
	}

	// 트래버설 먼저, 실패하면 일반 점프.
	if (Parts.Traversal && Parts.Traversal->TryTraversalAction())
	{
		VB_LOG(Verbose, "[Jump] 점프 누름 - 트래버설로 씀");
		return;
	}

	if (bJumpedOutOfSlide && Body.bIsCrouched)
	{
		// 일어서기를 기다리는 시간(옮기기 전 캐릭터 클래스의 상수).
		constexpr double SlideJumpWindowSeconds = 0.25;
		SlideJumpRequest.PressOutOfSlide(Body.GetWorld()->GetTimeSeconds(), SlideJumpWindowSeconds);
		return;
	}
	// 여기서 막히면 엔진이 조용히 버리므로 누를 때 이유를 남긴다.
	if (!Body.CanJump())
	{
		VB_LOG(Log, "[Jump] 점프 누름 - 엔진이 점프를 거절함(CanJump 거짓: 앉음 %d, 점프 횟수 %d/%d, 미끄럼 점프 대기 %d)",
			Body.bIsCrouched ? 1 : 0, Body.JumpCurrentCount, Body.JumpMaxCount, SlideJumpRequest.IsIdle() ? 0 : 1);
	}

	Body.Jump();
}

void FVBJumpCrouchInput::JumpReleased(AVBCharacter& Body)
{
	bJumpInputHeld = false;
	if (SlideJumpRequest.Release())
	{
		return; // 들고 있는 점프는 이어 준 뒤에 멈춘다
	}

	Body.StopJumping();
}

void FVBJumpCrouchInput::CrouchPressed(const FParts& Parts)
{
	AVBCharacter& Body = *Parts.Body;
	if (Parts.Interactor && Parts.Interactor->OwnsCapsuleFacing())
	{
		return;
	}
	// 매달린 채 앉기 = 놓기. 점프 홀드도 푼다(누른 채 떨어지면 같은 렛지를 다시 잡는다).
	if (Parts.Traversal && Parts.Traversal->IsHanging())
	{
		bJumpInputHeld = false;
		Parts.Traversal->RequestHangDrop();
		return;
	}
	// 등반 중 크라우치 = 벽에서 놓기. 매달림과 같은 규약이다.
	if (Parts.Traversal && Parts.Traversal->IsClimbing())
	{
		bJumpInputHeld = false;
		Parts.Traversal->RequestClimbDrop();
		return;
	}
	// 사다리에서 앉기 = 놓기.
	if (Parts.Traversal && Parts.Traversal->IsOnLadder())
	{
		bJumpInputHeld = false;
		Parts.Traversal->RequestLadderDrop();
		return;
	}
	// 자세 관문 - 몸통을 쓰는 동작 중에는 앉기, 일어서기를 막고 한 줄 남긴다(막는 목록은 전투 규칙 표의 앉기 칸).
	//   앉기 능력은 몽타주를 멈추지 않으므로 그대로 두면 서기 몽타주 + 앉은 캡슐이 된다.
	FGameplayTagContainer PostureBlockers;
	if (const UVBAbilitySystemComponent* ASC = Parts.AbilitySystem;
		ASC && ASC->IsBlockedByHeldState(VBGameplayTags::Ability_Locomotion_Crouch, &PostureBlockers))
	{
		VB_LOG(Log, "자세 전환 막음(%s) - %s", Body.bIsCrouched ? TEXT("일어서기") : TEXT("앉기"), *PostureBlockers.ToStringSimple());
		return;
	}

	if (const UAnimInstance* Anim = Body.GetMesh() ? Body.GetMesh()->GetAnimInstance() : nullptr; Anim && Anim->IsAnyMontagePlaying())
	{
		const UAnimMontage* Playing = Anim->GetCurrentActiveMontage();
		VB_LOG(Log, "자세 전환 막음(%s) - 몽타주 %s 재생 중", Body.bIsCrouched ? TEXT("일어서기") : TEXT("앉기"), *GetNameSafe(Playing));
		return;
	}

	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(Body.GetCharacterMovement());
		SlideCMC && SlideCMC->IsSliding())
	{
		SlideCMC->CancelSlide(true);
		return;
	}
	// Sprint+Crouch: 기존 입력이 예측 CMC 슬라이드를 시작한다. 일반 웅크리기는 여전히 GAS 를 쓴다.
	if (Parts.LocomotionIntent->GetIsSprinting())
	{
		if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(Body.GetCharacterMovement());
			SlideCMC && SlideCMC->CanRequestSlide() &&
			(!Body.GetMesh() || !Body.GetMesh()->GetAnimInstance() || !Body.GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()))
		{
			Body.Crouch();
			SlideCMC->RequestSlide();
			return;
		}
	}

	Parts.Hero->DispatchAbilityInput(VBGameplayTags::Input_Crouch);
}

void FVBJumpCrouchInput::TickSlideJump(AVBCharacter& Body, bool bSceneAllowed)
{
	// 미끄럼을 끊고 누른 점프: 요청이 전이를 정하고 캐릭터가 행동을 적용한다.
	const UCharacterMovementComponent* JumpCMC = Body.GetCharacterMovement();
	FVBSlideJumpRequest::FTickInput JumpFacts;
	JumpFacts.Now = Body.GetWorld()->GetTimeSeconds();
	JumpFacts.bSceneAllowed = bSceneAllowed;
	JumpFacts.bHasMovement = JumpCMC != nullptr;
	JumpFacts.bMovingOnGround = JumpCMC && JumpCMC->IsMovingOnGround();
	JumpFacts.bCrouched = Body.bIsCrouched;
	JumpFacts.bWasJumping = Body.bWasJumping;
	JumpFacts.bPressedJump = Body.bPressedJump;
	JumpFacts.bJumpHeld = bJumpInputHeld;
	const FVBSlideJumpRequest::FActions Actions = SlideJumpRequest.Tick(JumpFacts);
	if (Actions.bClearHeld)
	{
		bJumpInputHeld = false;
	}

	if (Actions.bStopJumping)
	{
		Body.StopJumping();
	}

	if (Actions.bJump)
	{
		Body.Jump();
	}
}

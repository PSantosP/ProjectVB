// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBBodyControlPolicy.h"

#include "Character/VBPlayerRagdollComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Traversal/VBTraversalComponent.h"

namespace VBBodyControlPolicy
{
	bool IsCapsuleRotationOwned(const FCapsuleRotationFacts& Facts)
	{
		// 하나라도 회전을 점유하면 물리가 캡슐을 돌리지 않는다.
		return Facts.bFinisherScene || Facts.bPlayerRagdoll || Facts.bSmartObjectFacing || Facts.bEnvironmentMotionFacing
			|| Facts.bAttackWarp || Facts.bTraversalAction || Facts.bTraversalFacing || Facts.bDeathPresented;
	}

	FCapsuleRotationFacts GatherCapsuleRotationFacts(const FBodyControlSources& Sources)
	{
		// 전부 상태를 안 바꾸는 조회다(2-1 설계 4절).
		FCapsuleRotationFacts Facts;
		Facts.bFinisherScene = Sources.bFinisherScene;
		Facts.bPlayerRagdoll = Sources.PlayerRagdoll && Sources.PlayerRagdoll->IsRagdollActive();
		Facts.bSmartObjectFacing = Sources.SmartObject && Sources.SmartObject->OwnsCapsuleFacing();
		Facts.bEnvironmentMotionFacing = Sources.Interactor && Sources.Interactor->OwnsCapsuleFacing();
		Facts.bAttackWarp = Sources.bAttackWarp;
		Facts.bTraversalAction = Sources.Traversal && Sources.Traversal->IsDoingTraversal();
		Facts.bTraversalFacing = Sources.Traversal && Sources.Traversal->OwnsCapsuleFacing();
		Facts.bDeathPresented = Sources.bDeathPresented;
		return Facts;
	}

	bool BlocksSlideJump(const FSlideJumpSceneFacts& Facts)
	{
		return Facts.bEnvironmentMotionFacing || Facts.bSmartObjectInteracting || Facts.bPlayerRagdoll || Facts.bFinishing
			|| Facts.bDead || Facts.bTraversalAction || Facts.bTraversalFacing;
	}

	FSlideJumpSceneFacts GatherSlideJumpSceneFacts(const FBodyControlSources& Sources)
	{
		FSlideJumpSceneFacts Facts;
		Facts.bEnvironmentMotionFacing = Sources.Interactor && Sources.Interactor->OwnsCapsuleFacing();
		Facts.bSmartObjectInteracting = Sources.SmartObject && Sources.SmartObject->IsInteracting();
		Facts.bPlayerRagdoll = Sources.PlayerRagdoll && Sources.PlayerRagdoll->IsRagdollActive();
		Facts.bFinishing = Sources.bFinishing;
		Facts.bDead = Sources.bDead;
		Facts.bTraversalAction = Sources.Traversal && Sources.Traversal->IsDoingTraversal();
		Facts.bTraversalFacing = Sources.Traversal && Sources.Traversal->OwnsCapsuleFacing();
		return Facts;
	}

	FRotationMode ResolveRotationMode(const FRotationModeInputs& Inputs)
	{
		const bool bStrafe = !Inputs.bSprinting && (Inputs.bWantsToStrafe || Inputs.bWantsToAim || Inputs.bLockedOn || Inputs.bInCombat);
		FRotationMode Mode;
		// 엔진은 방향 칸이 켜져 있고 (루트모션 중이면) 루트모션 회전 칸도 켜져야 캡슐을 돌린다 - 소유되면 셋 다 끈다.
		Mode.bOrientRotationToMovement = !bStrafe && !Inputs.bCapsuleRotationOwned;
		Mode.bUseControllerDesiredRotation = bStrafe && !Inputs.bCapsuleRotationOwned;
		// 켜 두는 이유: 꺼진 동안 생긴 캡슐-컨트롤 격차가 몽타주 끝에 한 번에 풀려 캐릭터가 휙 돈다.
		Mode.bAllowPhysicsRotationDuringAnimRootMotion = !Inputs.bCapsuleRotationOwned;
		return Mode;
	}

	bool BlocksLockFacing(const FCapsuleRotationFacts& Facts)
	{
		// 캡슐 회전 목록에서 공격 워프, 트래버설 동작, 사망 연출을 뺀 것 - 그 창에서도 락온의 캐릭터 회전과 카메라는 그대로다.
		return Facts.bTraversalFacing || Facts.bFinisherScene || Facts.bPlayerRagdoll || Facts.bSmartObjectFacing
			|| Facts.bEnvironmentMotionFacing;
	}

	bool BlocksCombatCamera(const FCapsuleRotationFacts& Facts)
	{
		return BlocksLockFacing(Facts);
	}

	bool BlocksEnvironmentCameraComposition(const FEnvironmentCameraFacts& Facts)
	{
		// 처형, 스마트 오브젝트, 환경 상호작용 중에도 구도는 바꾼다. 전투 카메라 목록을 쓰면 그 셋이 새로 막힌다.
		return Facts.bPlayerRagdoll || Facts.bTraversalFacing;
	}

	bool IsPoseOwnedByMontage(const FMontagePoseFacts& Facts)
	{
		// 사망, 처형, 매달림 포즈는 TIP 이 덮지 않는다 - 끊기면 시체가 일어서거나 처형이 안 나온다
		return Facts.bDead || Facts.bFinisherScene || Facts.bTraversalPose;
	}

	bool IsAttentionPoseSuppressed(const FAttentionPoseFacts& Facts)
	{
		return Facts.bDead || Facts.bTraversalAction || Facts.bTraversalFacing || Facts.bEnvironmentMotionFacing
			|| Facts.bAttacking || Facts.bDodgeCommit || Facts.bHitReacting || Facts.bStaggered || Facts.bDowned
			|| Facts.bInteracting;
	}

	bool BlocksPlayerLookAt(const FPlayerLookAtFacts& Facts)
	{
		return Facts.bMeshSimulatingPhysics || Facts.bDead || Facts.bFinisherPlaying || Facts.bPlayerRagdoll
			|| Facts.bSmartObjectInteracting || Facts.bAttentionSuppressed;
	}

	bool BlocksEnemyLookAt(const FEnemyLookAtFacts& Facts)
	{
		return Facts.bDead || Facts.bRecoverableRagdoll || Facts.bFinisherScene
			|| Facts.bStaggered || Facts.bDowned || Facts.bHitReacting || Facts.bAttacking;
	}

	bool BlocksTraversalStart(const FTraversalStartFacts& Facts)
	{
		return Facts.bSmartObjectInteracting || Facts.bPlayerRagdoll;
	}

	bool BlocksAppearanceSwap(const FAppearanceSwapFacts& Facts)
	{
		return Facts.bDead || Facts.bFinisherScene || Facts.bPlayerRagdoll || Facts.bTraversalAction || Facts.bTraversalFacing
			|| Facts.bSmartObjectInteracting || Facts.bAttacking || Facts.bDodging || Facts.bInteracting || Facts.bWeaponTransition;
	}
}

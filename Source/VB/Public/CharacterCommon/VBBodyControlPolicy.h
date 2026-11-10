// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UVBPlayerRagdollComponent;
class UVBGASPSmartObjectComponent;
class UVBInteractorComponent;
class UVBTraversalComponent;

// 캐릭터(캡슐 회전, 방향, 애님 반응, 카메라 구도, 시선)를 지금 어느 기능이 제어하나 - 소비처별 판정이 여기 있다. 상태가 없다.
// 소비처가 사실을 모아 전달하고 여기서는 막는지만 정한다. 새 소유자를 더할 때는 이 파일의 정책을 전부 훑는다.
namespace VBBodyControlPolicy
{
	/** 캡슐 회전 소유 - 기능 하나라도 점유하면 물리가 캡슐을 돌리지 않는다. 이동 컴포넌트가 CMC 회전 칸 셋에 적용한다. */
	struct FCapsuleRotationFacts
	{
		bool bFinisherScene = false; // UVBFinisherComponent::IsActorInScene
		bool bPlayerRagdoll = false; // UVBPlayerRagdollComponent::IsRagdollActive - 일어나는 중 포함
		bool bSmartObjectFacing = false; // UVBGASPSmartObjectComponent::OwnsCapsuleFacing
		bool bEnvironmentMotionFacing = false; // UVBInteractorComponent::OwnsCapsuleFacing
		bool bAttackWarp = false; // AVBCharacter::IsWarpOwningRotation
		bool bTraversalAction = false; // UVBTraversalComponent::IsDoingTraversal - 몽타주 동작
		bool bTraversalFacing = false; // UVBTraversalComponent::OwnsCapsuleFacing - 매달림, 등반, 사다리
		bool bDeathPresented = false; // AVBCharacter 의 사망 연출(모든 기계)
	};
	VB_API bool IsCapsuleRotationOwned(const FCapsuleRotationFacts& Facts);

	/** CMC 회전 칸 셋의 입력과 결과. 질주는 이동 방향, 스트레이프, 조준, 락온, 전투 중이면 카메라 방향, 캡슐 회전이 소유되면 셋 다 끈다. */
	struct FRotationModeInputs
	{
		bool bSprinting = false;
		bool bWantsToStrafe = false;
		bool bWantsToAim = false;
		bool bLockedOn = false;
		bool bInCombat = false;
		bool bCapsuleRotationOwned = false;
	};
	struct FRotationMode
	{
		bool bOrientRotationToMovement = false;
		bool bUseControllerDesiredRotation = false;
		bool bAllowPhysicsRotationDuringAnimRootMotion = false;
	};
	VB_API FRotationMode ResolveRotationMode(const FRotationModeInputs& Inputs);

	/** 플레이어 캐릭터의 사실을 모을 원천. 호출 동안만 참조하고, 포인터가 없으면 그 칸은 false. AVBCharacter::MakeBodyControlSources 가 채운다. */
	struct FBodyControlSources
	{
		const UVBPlayerRagdollComponent* PlayerRagdoll = nullptr;
		const UVBGASPSmartObjectComponent* SmartObject = nullptr;
		const UVBInteractorComponent* Interactor = nullptr;
		const UVBTraversalComponent* Traversal = nullptr;
		bool bFinisherScene = false; // UVBFinisherComponent::IsActorInScene
		bool bAttackWarp = false; // AVBCharacter::IsWarpOwningRotation
		bool bFinishing = false; // UVBAbilitySystemStatics::IsActorFinishing - 처형 장면(IsActorInScene)과 다르다
		bool bDead = false; // AVBCharacter::IsDead (판정은 UVBAbilitySystemStatics::IsDead 하나)
		bool bDeathPresented = false; // AVBCharacter 의 사망 연출 - 캡슐 회전만 읽는다
	};
	VB_API FCapsuleRotationFacts GatherCapsuleRotationFacts(const FBodyControlSources& Sources);

	/** 미끄럼을 끊고 점프를 이어 줘도 되나. 막히면 FVBSlideJumpRequest 가 요청을 버린다. 처형은 처형 태그로 본다. */
	struct FSlideJumpSceneFacts
	{
		bool bEnvironmentMotionFacing = false; // UVBInteractorComponent::OwnsCapsuleFacing
		bool bSmartObjectInteracting = false; // UVBGASPSmartObjectComponent::IsInteracting
		bool bPlayerRagdoll = false; // 일어나는 중 포함
		bool bFinishing = false;
		bool bDead = false;
		bool bTraversalAction = false; // UVBTraversalComponent::IsDoingTraversal
		bool bTraversalFacing = false; // 매달림, 등반, 사다리
	};
	VB_API bool BlocksSlideJump(const FSlideJumpSceneFacts& Facts);
	VB_API FSlideJumpSceneFacts GatherSlideJumpSceneFacts(const FBodyControlSources& Sources);

	/** 락온의 캐릭터 조건 - 패링 때 캐릭터 회전을 막나. 공격 워프, 트래버설 동작, 사망 연출은 막지 않는다(그 창에도 락온은 그대로다). */
	VB_API bool BlocksLockFacing(const FCapsuleRotationFacts& Facts);
	/** 전투 카메라의 캐릭터 조건. 카메라 자격(잠금 가능, 로컬 조종, 조준, 뷰타깃)은 소비처에 남는다. */
	VB_API bool BlocksCombatCamera(const FCapsuleRotationFacts& Facts);

	/** 환경 카메라 구도 변경. 조준은 소비처 조건이다. */
	struct FEnvironmentCameraFacts
	{
		bool bPlayerRagdoll = false;
		bool bTraversalFacing = false;
	};
	VB_API bool BlocksEnvironmentCameraComposition(const FEnvironmentCameraFacts& Facts);

	/** 저작된 포즈가 메시 제어를 점유하고 있나 - 제자리 회전(TIP) 몽타주를 막는다(UVBAnimInstance::bPoseOwnedByMontage). */
	struct FMontagePoseFacts
	{
		bool bDead = false; // UVBAbilitySystemStatics::IsActorDead
		bool bFinisherScene = false;
		bool bTraversalPose = false; // UVBAnimInstance::IsTraversalOwningPose - 애님이 미러한 값
	};
	VB_API bool IsPoseOwnedByMontage(const FMontagePoseFacts& Facts);

	/** 플레이어 주시 자세 억제(UVBPlayerAttentionComponent::IsPoseSuppressed). 소유자가 없을 때의 처리는 소비처가 한다. */
	struct FAttentionPoseFacts
	{
		bool bDead = false; // UVBAbilitySystemStatics::IsActorDead
		bool bTraversalAction = false;
		bool bTraversalFacing = false;
		bool bEnvironmentMotionFacing = false;
		bool bAttacking = false; // State.Combat.Attacking - 아래 여섯은 ASC 가 없으면 false
		bool bDodgeCommit = false; // State.Combat.DodgeCommit
		bool bHitReacting = false; // State.Combat.HitReacting
		bool bStaggered = false; // State.Combat.Staggered
		bool bDowned = false; // State.Combat.Downed
		bool bInteracting = false; // State.Interacting
	};
	VB_API bool IsAttentionPoseSuppressed(const FAttentionPoseFacts& Facts);

	/** 플레이어 시선 POI(UVBLookAtPOIComponent::ResolveTarget). 켜짐, 캐릭터, 메시 확인은 소비처가 먼저 한다. */
	struct FPlayerLookAtFacts
	{
		bool bMeshSimulatingPhysics = false; // USkeletalMeshComponent::IsAnySimulatingPhysics
		bool bDead = false; // ASC 의 State.Dead 태그를 직접 읽는다 - IsActorDead 와 같은 ASC, 같은 태그(엔진 GetAbilitySystemComponent 는
		                                       //   GetAbilitySystemComponentFromActor 를 그대로 부른다). 한 판정으로 합치는 일은 VBLookAtPOIComponent 쪽
		bool bFinisherPlaying = false; // UVBFinisherComponent::IsPlaying - 컴포넌트가 없으면 false
		bool bPlayerRagdoll = false;
		bool bSmartObjectInteracting = false; // UVBGASPSmartObjectComponent::IsInteracting - Facing 과 다르다
		bool bAttentionSuppressed = false; // UVBPlayerAttentionComponent::IsPoseSuppressed 의 결과(소유자 없음 처리까지 거친 값)
	};
	VB_API bool BlocksPlayerLookAt(const FPlayerLookAtFacts& Facts);

	/** 적 시선의 적 전용 차단. 대상 사망 검사와 부모 POI 의 차단은 소비처에서 그대로 이어진다. */
	struct FEnemyLookAtFacts
	{
		bool bDead = false; // AVBEnemyBase::IsDead
		bool bRecoverableRagdoll = false; // UVBEnemyRagdollComponent::IsRecoverableActive
		bool bFinisherScene = false;
		bool bStaggered = false;
		bool bDowned = false;
		bool bHitReacting = false;
		bool bAttacking = false;
	};
	VB_API bool BlocksEnemyLookAt(const FEnemyLookAtFacts& Facts);

	/** 트래버설 시작(UVBTraversalComponent::TryTraversalAction). 이미 동작 중인지는 소비처가 본다. */
	struct FTraversalStartFacts
	{
		bool bSmartObjectInteracting = false;
		bool bPlayerRagdoll = false;
	};
	VB_API bool BlocksTraversalStart(const FTraversalStartFacts& Facts);

	/** 겉모습 교체 판정(서버). 동작 중 교체는 무기 재부착, 뼈 갱신이 엇갈려 평상시에만 받는다. */
	struct FAppearanceSwapFacts
	{
		bool bDead = false;
		bool bFinisherScene = false;
		bool bPlayerRagdoll = false; // 일어나는 중 포함
		bool bTraversalAction = false;
		bool bTraversalFacing = false; // 매달림, 등반, 사다리
		bool bSmartObjectInteracting = false;
		bool bAttacking = false; // State.Combat.Attacking - ASC 가 없으면 false
		bool bDodging = false; // State.Combat.Dodging
		bool bInteracting = false; // State.Interacting
		bool bWeaponTransition = false; // State.Weapon.Summoning / Sheathing
	};
	VB_API bool BlocksAppearanceSwap(const FAppearanceSwapFacts& Facts);
}

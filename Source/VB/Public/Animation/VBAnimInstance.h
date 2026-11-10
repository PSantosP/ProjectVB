// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBAnimationTypes.h"
#include "Types/VBWeaponStateTypes.h"
#include "Curves/CurveFloat.h"
#include "CharacterTrajectoryComponent.h"
#include "AnimationWarpingTypes.h"
#include "BoneControllers/AnimNode_OrientationWarping.h" // EOrientationWarpingSpace
#include "GameplayTagContainer.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "Animation/VBInteractionAnimInterface.h"
#include "Animation/VBInteractionIK.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryTypes.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Animation/VBGASPNotifyInterface.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "Animation/VBAnimGuardHandoff.h"
#include "Animation/VBAnimPoolInterruptLatch.h"
#include "Animation/VBAnimPoseSlots.h"
#include "Animation/VBAnimSlideHandoff.h"
#include "VBAnimInstance.generated.h"

class UPoseSearchDatabase;

class ACharacter;
class AVBCharacter;
class UCharacterMovementComponent;

// 플레이어와 적이 함께 쓰는 AnimInstance: 매 프레임 게임 사실을 모아 애님 그래프가 이름으로 읽는 값과 핀 함수를 낸다.
// 계산은 VBAnimLocomotionRules, VBAnimMotionMatchingRules 가, 슬라이드, 가드, 풀 교체 상태는 FVBAnim 묶음이 맡는다.
// UPROPERTY 는 그래프가 이름으로 읽으므로 옮기지 않는다.
UCLASS()
class VB_API UVBAnimInstance : public UAnimInstance, public IVBInteractionAnimInterface, public IVBGASPNotifyInterface
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

	// 공격 루트모션은 실제 이동으로 유지하되 무입력 복귀 포즈의 주행 의도로 쓰지 않는다.
	static bool ShouldSuppressAttackLocomotion(const ACharacter* Character);
	bool IsSuppressingAttackLocomotion() const { return bSuppressAttackLocomotion; }
	virtual EVBGait GetGASPNotifyGait_Implementation() const override { return Gait; }
	EVBGait GetCurrentGait() const { return Gait; }
	virtual void RequestGASPEarlyTransition_Implementation(bool bToLoop) override;
	bool UsesWeaponLocomotion() const { return LocomotionState != EVBLocomotionState::Unarmed; }
	bool ShouldReleaseGASPRootOffset() const { return bReleaseGASPRootOffset; }
	bool CanUseGuardLocomotionPose() const;
	UChooserTable* GetRequestedGuardLocomotionChooser() const;
	// 트래버설이 캡슐 위치를 제어하고 있나. OffsetRootBone 두 모드 판정이 같이 쓰도록 한 곳에 둔다.
	bool IsTraversalOwningPose() const { return bIsHanging || bIsClimbing || bIsOnLadder; }
	// 앞 렛지 트랜스폼의 유일한 C++ 입구. 트래버설 컴포넌트가 MotionMatch 직전에 넣고 같은 호출에서 읽힌다.
	void SetInteractionTransform(const FTransform& InTransform) { InteractionTransform = InTransform; }
	// 지금 MM 이 고른 DB(진단용).
	const UPoseSearchDatabase* GetCurrentSelectedDatabase() const { return CurrentSelectedDatabase; }
	// 연결된 GASP 그래프의 CharacterProperties 가 읽는다.
	bool HasJustLanded() const { return JustLanded; }
	const FVector& GetLandVelocity() const { return LandVelocity; }

	// 래그돌 포즈 핀.
	UFUNCTION(BlueprintPure, Category="VB|Ragdoll", meta=(BlueprintThreadSafe))
	double GetRagdollElapsedTime() const { return RagdollState.ElapsedSeconds; }
	UFUNCTION(BlueprintPure, Category="VB|Ragdoll", meta=(BlueprintThreadSafe))
	bool IsPoweredRagdollPose() const { return RagdollState.bPowered; }
	UFUNCTION(BlueprintPure, Category="VB|Ragdoll", meta=(BlueprintThreadSafe))
	bool IsNotPoweredRagdollPose() const { return !RagdollState.bPowered; }

	// 연결된 GASP 그래프.
	UFUNCTION(BlueprintPure, Category="VB|GASP", meta=(BlueprintThreadSafe))
	UAnimInstance* GetGASPLocomotionInstance() const { return GASPLocomotionInstance; }

	UFUNCTION(BlueprintPure, Category="VB|GASP", meta=(BlueprintThreadSafe))
	UAnimInstance* GetGASPNotifyInstance() const { return bUseGASPSlidePose ? GASPSlideInstance : GASPLocomotionInstance; }

	// GASP 핀 바인딩. 핀 바인딩은 워커 스레드에서 불리므로 BlueprintThreadSafe 가 필수다.

	// 현재 속도와 예측 궤적 속도가 모두 0 이 아니면 true.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool IsMoving() const;

	// OffsetRootBone.TranslationMode 핀. 지상 이동 중이면 Interpolate, 몽타주, 정지, 공중이면 Release.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EOffsetRootBoneMode GetOffsetRootTranslationMode() const;

	// OffsetRootBone.RotationMode 핀. 몽타주 중이면 Release, 아니면 Accumulate.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EOffsetRootBoneMode GetOffsetRootRotationMode() const;

	// OffsetRootBone.TranslationHalflife 핀. MMState 별 HalfLife 값을 고른다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float GetOffsetRootTranslationHalfLife() const;

	// MotionMatching 상태 판정.

	// 이동을 막 시작했나(Starts DB 선택).
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool IsStarting() const;

	// 가속과 속도의 각도 차가 큰 급회전인가(Pivots DB 선택).
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool IsPivoting() const;

	// 옛 VB MM chooser 용 제자리 회전 판정. 루트와 캡슐 yaw 차가 ShouldTurnInPlaceYawDelta 이상이면 true.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool ShouldTurnInPlace() const;

	// 출발 시 메시 방향과 목표 방향 차가 커서 SpinTransition 을 쓸지.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool ShouldSpinTransition() const;

	// Steering 노드 활성 여부.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool EnableSteering() const;

	// OrientationWarping 의 WarpingSpace 핀. OffsetRootBone 이 켜져 있으면 RootBoneTransform.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EOrientationWarpingSpace GetOrientationWarpingWarpingSpace() const;

	// 캐릭터 로컬 기준 [-1..1] 로 정규화한 가속도.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FVector CalculateRelativeAccelerationAmount() const;

	// 기울기 블렌드스페이스 입력. 옆 가속도에 속도 비율을 곱한다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FVector2D GetLeanAmount() const;

	// BlendSpacePlayer.X 핀용 GetLeanAmount().X.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float GetLeanAmountX() const;

	// 에임오프셋 입력. 루트와 조준 회전의 차(Yaw, Pitch).
	UFUNCTION(BlueprintPure, Category="VB|Animation|AimOffset", meta=(BlueprintThreadSafe))
	FVector2D GetAOValue() const;

	// 에임오프셋을 켤지. Apply Mesh Space Additive 의 Alpha 입력.
	UFUNCTION(BlueprintPure, Category="VB|Animation|AimOffset", meta=(BlueprintThreadSafe))
	bool EnableAO() const;

	// BlendSpacePlayer.X/Y 핀용으로 GetAOValue 를 나눈 값.
	UFUNCTION(BlueprintPure, Category="VB|Animation|AimOffset", meta=(BlueprintThreadSafe))
	float GetAOValueX() const;

	UFUNCTION(BlueprintPure, Category="VB|Animation|AimOffset", meta=(BlueprintThreadSafe))
	float GetAOValueY() const;

	// MotionMatching 노드의 Blend Time. 상황별 MM*BlendTime 중 하나를 고른다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float GetMMBlendTime() const;

	// MM 검색 InterruptMode. 상태가 바뀐 프레임에만 끊는다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EPoseSearchInterruptMode GetMMInterruptMode() const;

	// 방금 가볍게/세게 착지했나(LandVelocity 기준).
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool JustLandedLight() const;

	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool JustLandedHeavy() const;

	// 트래버설 직후인가(FromTraversal DB 선택).
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool JustTraversed() const;

	// 무장 chooser 용 착지 예상 시간. 지상 0, 상승 999, 하강 LandPredictionHeight / |Vz|.
	// 비무장 InAir 는 TrajectoryCollision.TimeToLand 를 쓴다 - 둘을 일부러 나눠 둔다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float TimeToLand() const;

	// MM 이 한 번에 검색할 DB 목록(null 제외).
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	TArray<UPoseSearchDatabase*> GetAllActivePoseSearchDBs() const;

	// MM 노드의 검색 결과에서 선택된 DB 와 태그를 캐시한다(PostSelection 에서 호출).
	UFUNCTION(BlueprintCallable, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	void CacheMMSelectedDatabase(UPARAM(ref) const FAnimNodeReference& Node);

	// 궤적의 미래 샘플에서 읽은 위치, 방향. BP 의 GetDesiredFacing 과 이름이 겹치지 않게 Transform 을 붙였다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FTransform GetDesiredFacingTransform() const;

	// Steering.TargetOrientation 핀용 회전(FQuat).
	UFUNCTION(BlueprintPure, Category="VB|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FQuat GetDesiredFacingOrientation() const;

	// 처형 상호작용 (GASP 5.8 Pose Search Interaction).

	// IVBInteractionAnimInterface 구현.
	virtual const FPoseSearchBlueprintResult& GetInteractionResult() const override { return InteractionResult; }
	virtual void SetInteractionResult(const FPoseSearchBlueprintResult& InResult) override { InteractionResult = InResult; }

	// MotionMatchMulti 에 넘길 Pose History 노드 이름.
	FName GetInteractionPoseHistoryName() const { return InteractionPoseHistoryName; }

	// 원본 FullBodyIK Control Rig 에 위치와 회전을 같은 컴포넌트 공간으로 전달한다.
	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FTransform GetInteractionIKLeftTarget() const { return InteractionIK.Left.Target; }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FTransform GetInteractionIKRightTarget() const { return InteractionIK.Right.Target; }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	bool IsInteractionIKEnabled() const { return InteractionIK.bEnabled; }

	// 상호작용 손 IK 의 AnimGraph 핀. 핀은 함수만 받아서 구조체 멤버를 함수로 낸다. TwoBoneIK 에는 위치만 전달한다.
	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FVector GetInteractionIKLeftEffector() const { return InteractionIK.Left.Target.GetLocation(); }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	float GetInteractionIKLeftAlpha() const { return InteractionIK.bEnabled ? InteractionIK.Left.Alpha : 0.0f; }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FVector GetInteractionIKRightEffector() const { return InteractionIK.Right.Target.GetLocation(); }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	float GetInteractionIKRightAlpha() const { return InteractionIK.bEnabled ? InteractionIK.Right.Alpha : 0.0f; }

	// 처형 chooser 를 평가해 검색할 PSD 후보를 낸다. 없으면 빈 배열. 공격자의 UVBFinisherComponent 가 부른다.
	void EvaluateFinisherDatabases(TArray<UPoseSearchDatabase*>& OutDatabases) const;

	// 공개 값: 래그돌 상태와 GASP 그래프.

	UPROPERTY(BlueprintReadOnly, Category="VB|Ragdoll")
	FVBRagdollAnimationState RagdollState;

	// 연결된 GASP 그래프가 기본 로코모션, 루트 오프셋, 회전 선택을 맡는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GASP")
	bool bUseGASPLocomotion = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GASP|FootPlacement", meta=(ClampMin="1.0", Units="cm"))
	float FootPlacementPelvisMaxOffset = 15.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GASP|FootPlacement", meta=(ClampMin="1.0"))
	float FootPlacementPelvisStiffness = 10000.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category="VB|GASP")
	bool bUseGASPSlidePose = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="VB|GASP")
	bool bUseOriginalGASPUnarmed = true;

	// 가드 유지 구간에 전용 가드 Idle/Turn 선택이 성공한 뒤 하체를 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GASP|Guard")
	bool bEnableGuardTurnPose = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="VB|GASP|Guard")
	bool bUseGuardLocomotionPose = false;

	// UpdateGuardHandoff(4단계)가 쓰고 연결된 GASP 그래프와 AVBCharacter 가 읽는다. 이 인스턴스의 OffsetRootBone 은 안 읽는다.
	UPROPERTY(Transient)
	bool bReleaseGASPRootOffset = false;

	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> GASPSlideInstance;

	// 워커 스레드 갱신 전에 게임 스레드에서 새로 잡는다.
	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> GASPLocomotionInstance;

	// 사다리에 붙어 있나. 트래버설 컴포넌트의 미러이고 자세는 몽타주가 제어한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	bool bIsOnLadder = false;

	// 공개 값: 에임오프셋과 MotionMatching 저작.

	// 에임오프셋을 켜는 yaw 한계.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|AimOffset")
	float EnableAOYawThreshold = 115.0f;

	// DefaultSlot 가중치가 이 이상이면 에임오프셋을 끈다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|AimOffset")
	float EnableAOSlotWeightThreshold = 0.5f;

	// GetDesiredFacing 이 볼 미래 샘플 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float DesiredFacingLookaheadSeconds = 0.4f;

	// 정지/이동별 궤적 생성 값(ABP 기본값에서 저작). Moving 은 0/0/0 유지 - 엔진 기본 10/70 이면 마우스 yaw 로 MM 이 진동한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching|Trajectory")
	FPoseSearchTrajectoryData TrajectoryGenerationData_Idle;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching|Trajectory")
	FPoseSearchTrajectoryData TrajectoryGenerationData_Moving;

	// MM 이 한 번에 검색할 PSD 목록(에디터에서 채움).
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	TArray<TObjectPtr<UPoseSearchDatabase>> ActivePoseSearchDatabases;

	// 매 프레임 평가해 검색할 PSD 를 고르는 마스터 chooser.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	TObjectPtr<class UChooserTable> MasterChooserTable;

	// PoseSearchGenerateTransformTrajectory 가 ref 로 고쳐 쓰는 엔진용 yaw 상태. 궤적 샘플링 간격, 개수는 UpdateTrajectory 의 호출 인자에만 있다.
	float DesiredControllerYawLastUpdate = 0.0f;

	// 공개 값: 처형 상호작용.

	// 처형 후보 PSD 를 고르는 chooser(컨텍스트 = 이 AnimInstance). 공격자 쪽만 쓰고, 비면 이 경로를 건너뛴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	TObjectPtr<class UChooserTable> FinisherChooserTable;

	// MotionMatchMulti 가 읽을 Pose History 노드 이름. 틀리면 에러 없이 결과만 안 온다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	FName InteractionPoseHistoryName = FName("PoseHistory");

	// 상호작용 손 IK. 5단계가 처형 IK, 환경 손 IK 순서로 매 프레임 채우고 AnimGraph 가 핀으로 읽는다.
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category="VB|Finisher")
	FVBInteractionIKRuntime InteractionIK;

protected:
	// 이동 상태: chooser 와 그래프가 읽는 enum 과 전 프레임 값.

	// chooser 의 Idle/Moving 분기 값. UpdateMovementStateAndGait(7단계)가 쓴다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBMMState MMState = EVBMMState::Idle;

	// 전 프레임 MMState. 전환 감지용.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBMMState MMState_LastFrame = EVBMMState::Idle;

	// ABP 분기용 이동 모드, 회전 모드, 자세.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBMovementMode MovementMode = EVBMovementMode::Grounded;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBMovementMode MovementMode_LastFrame = EVBMovementMode::Grounded;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBRotationMode RotationMode = EVBRotationMode::Strafe;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBRotationMode RotationMode_LastFrame = EVBRotationMode::Strafe;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBStance Stance = EVBStance::Stand;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBStance Stance_LastFrame = EVBStance::Stand;

	// Walk/Run/Sprint. 멈추는 동안에도 마지막 Gait 를 유지한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBGait Gait = EVBGait::Run;

	// 이동 변수(ACharacter 공통 - 플레이어/적 모두 동작)

	// 전진(+)/후진(-) 속도 성분 - BlendSpace Y축
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float ForwardSpeed;

	// 우(+)/좌(-) 속도 성분 - BlendSpace X축
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float RightSpeed;

	// 지면 이동 속도(XY). Speed2D 와 같은 값이며 그래프 바인딩 확인 전까지 둘 다 둔다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float GroundSpeed;

	// Velocity.Size2D. GASP 이름을 따른다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float Speed2D = 0.0f;

	// 이번 프레임 캐릭터 속도(월드).
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector Velocity = FVector::ZeroVector;

	// 캐릭터 액터 트랜스폼.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FTransform CharacterTransform = FTransform::Identity;

	// 메시 root 본의 월드 트랜스폼.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FTransform RootTransform = FTransform::Identity;

	// 전 프레임 속도.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector Velocity_LastFrame = FVector::ZeroVector;

	// 실제 속도 변화량(입력 가속과 다르다).
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector VelocityAcceleration = FVector::ZeroVector;

	// 마지막으로 0 이 아니었던 속도. 멈출 때 방향 기준.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector LastNonZeroVelocity = FVector::ForwardVector;

	// 속도, 가속이 있는가.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool HasVelocity = false;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool HasAcceleration = false;

	// CMC 의 현재 입력 가속.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float AccelerationAmount = 0.0f;

	// 전 프레임 Gait.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBGait Gait_LastFrame = EVBGait::Run;

	// 전 프레임 MM 무기 풀. 주행 중 무기 스왑은 다른 상태가 안 바뀌어 이것으로만 감지된다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	EVBWeaponType MMWeaponType_LastFrame = EVBWeaponType::None;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	bool bFirePoolInterrupt = false; // 이번 프레임 발화 - GetMMInterruptMode(worker) 소비

	// Sprint 풀 합류 자격 = 질주 의도 || (자격 유지 중 Speed2D >= SprintPoolJoinSpeed).
	// 속도만 보면 발도 직후 감속 구간에 고속 클립이 잠깐 끼어든다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	bool bSprintPoolEligible = false;

	// 합류 하한은 UVBMovementConfig 프로파일에 있고, Dense chooser 의 Sprint/jog 경계도 같은 값으로 맞춘다.

	// 속도가 CMC 목표 최대속도에 이만큼(cm/s) 붙으면 보폭이 수렴한 것으로 보고 풀 교체를 발화한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float PoolInterruptSpeedSettleTolerance = 40.0f;

	// 이 속도(cm/s) 이하면 수렴으로 본다. 정지 중엔 목표 최대속도가 커서 위 허용오차로는 수렴하지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float PoolInterruptNearStopSpeed = 10.0f;

	// 수렴을 못 봐도 이 시간(초)이 지나면 강제로 발화한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float PoolInterruptTimeoutSeconds = 0.7f;

	// Dense chooser Sprint 행 게이트. 자격 없으면 0, 있으면 Speed2D.
	// 이 컬럼의 비-Sprint 행은 bNoMin/bNoMax 를 켜야 한다 - 빈 셀은 [0,0] 으로 읽힌다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	float MMSprintGateSpeed = 0.0f;

	// 공중 상태(점프/낙하)
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool IsFalling;

	// 이동 중인가(GroundSpeed 기준). AI 는 가속 입력이 없어 속도만 본다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool ShouldMove;

	// 앉기 상태(IsCrouched - 플레이어/적 공통)
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsCrouching;

	// 프레임 단계가 쓰는 값. 8-9 단계 값만 AVBCharacter 전용이다.

	// 전투모드(무기 상태 컴포넌트 미러). C++ 에선 안 읽으므로 지우려면 ABP 사용 여부부터 확인한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsCombatMode;

	// 스프린트 상태
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsSprinting;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float DistanceToGround;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsLockedOn = false;

	// Orientation Warping 입력
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	float LocomotionAngle = 0.0f;

	// 전 프레임 LocomotionAngle. 방향 전환 감지용.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	float PreviousLocomotionAngle = 0.0f;

	// MM 이 맞춰 볼 예측 궤적. UpdateTrajectory 가 매 프레임 만든다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	FTransformTrajectory Trajectory;

	// 궤적 충돌 결과. Dense InAir 의 Time to Land 가 이것을 읽는다 - 점프 정점에서도 값이 안 튄다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	FPoseSearchTrajectory_WorldCollisionResults TrajectoryCollision;

	// 궤적 미래 샘플의 속도. IsMoving 이 쓴다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	FVector FutureVelocity = FVector::ZeroVector;

	// OffsetRootBone 노드를 켤지.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	bool bOffsetRootBoneEnabled = true;

	// 마스터 chooser 의 Dense(0)/Sparse(1) 분기. CVar vb.MM.LOD 에서 매 프레임 읽는다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	int32 MMDatabaseLOD = 0;

	// PlayRate 조절
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	float WantedPlayRate = 1.0f;

	// 현재 선택된 DB. GASP 경로면 연결 그래프 값을 복사하고, 아니면 CacheMMSelectedDatabase 가 쓴다.
	UPROPERTY(BlueprintReadWrite, Category="VB|Animation|MotionMatching")
	TObjectPtr<const UPoseSearchDatabase> CurrentSelectedDatabase = nullptr;

	// 선택된 DB 의 태그. IsStarting / ShouldSpinTransition 이 검사한다.
	UPROPERTY(BlueprintReadWrite, Category="VB|Animation|MotionMatching")
	TArray<FName> CurrentDatabaseTags;

	// 판정 기준과 재생 튜닝(ABP 기본값에서 저작).

	// IsStarting 의 속도 차 기준(cm/s).
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float IsStartingVelocityBias = 100.0f;

	// IsPivoting 각도 기준(Strafe).
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float IsPivotingStrafeThreshold = 40.0f;

	// IsPivoting 각도 기준(Free). GASP 는 60° 지만 맨틀 직후 과속으로 회전각이 덜 나와 40° 로 낮췄다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float IsPivotingFreeThreshold = 40.0f;

	// ShouldTurnInPlace 의 루트/캡슐 yaw 차 기준.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float ShouldTurnInPlaceYawDelta = 50.0f;

	// ShouldSpinTransition 의 각도, 속도 기준.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float ShouldSpinTransitionYawDelta = 130.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float ShouldSpinTransitionMinSpeed = 150.0f;

	// RootTransform yaw 보정. 메시가 -Y 전방 관례라 +90°.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float RootTransformYawCorrection = 90.0f;

	// 착지 수직 속도가 이 이상이면 JustLandedHeavy.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float HeavyLandSpeedThreshold = 700.0f;

	// JustTraversed() 에 OR 된다. C++ 에선 안 쓰므로 지우려면 ABP 사용 여부부터 확인한다.
	UPROPERTY(BlueprintReadWrite, Category="VB|Animation|MotionMatching")
	bool bJustTraversedFlag = false;

	// 전 프레임 컨트롤러 yaw. 제자리 회전 판정용.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|MotionMatching")
	float PreviousDesiredControllerYaw = 0.0f;


	// GetMMBlendTime 의 상황별 블렌드 시간: 지상, 빠른 착지, 짧은 점프, 공중.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MMDefaultBlendTime = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MMFastLandBlendTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MMVeryShortJumpBlendTime = 0.15f;

	// 점프 직후를 지난 공중 상태에서 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MMInAirDefaultBlendTime = 0.5f;

	// Velocity.Z 가 이 이상이면 점프 시작으로 본다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MMJumpSpeedThreshold = 100.0f;

	// MMState 별 OffsetRoot 위치 HalfLife.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float OffsetRootTranslationHalfLife_Idle = 0.10f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float OffsetRootTranslationHalfLife_Moving = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float OffsetRootTranslationHalfLife_Default = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MinPlayRate = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float MaxPlayRate = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float RunAnimRootMotionSpeed = 500.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float WalkAnimRootMotionSpeed = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	FRuntimeFloatCurve SpeedToPlayRateCurve;

	// 로코모션 갈래와 그래프 핀 번호.

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;

	// ABP Blend Poses by int 인덱스. 0=비무장 MM, 1=무장 MM, 2=MM 풀 없는 발도 무기 SM, 3=crouch SM.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	int32 LocomotionStateIndex = 0;

	// 스테이트머신 경로(핀 2, 3)를 쓰는가. SM 경로에서는 OffsetRootBone 을 Release 한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bUsesStateMachinePath = false;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	EVBWeaponType CurrentWeaponType = EVBWeaponType::None;

	// MM 이 검색할 무기 풀(무기 데이터에서 옴, None = 기본 풀). CurrentWeaponType 과 다른 값이다.
	// 속도 프로파일도 이 값으로 골라 클립과 보폭이 어긋나지 않게 한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	EVBWeaponType MMWeaponType = EVBWeaponType::None;

	// 무기별 BlendListByInt 의 ActiveChildIndex 입력. 값의 의미는 아래 WeaponPoseSlots 가 정한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	int32 WeaponTypeIndex = 0;

	// crouch BlendListByInt 인덱스. 값의 의미는 아래 CrouchPoseSlots 가 정한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	int32 CrouchPoseIndex = 0;

	// 무기별 BlendListByInt 핀 순서. 인덱스가 핀 번호이고 핀을 추가할 때만 행을 추가한다.
	// enum 을 핀 번호로 쓰면 빈 enum 자리에서 엔진이 다른 무기 포즈로 조용히 클램프한다. None 은 경고 없이 0.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|PoseContract")
	TArray<EVBWeaponType> WeaponPoseSlots;

	// crouch 핀 순서. 인덱스 0 이 납도 공용 포즈라 None 항목을 둔다.
	// ArmedExplorationLocomotion SM 의 crouch 는 이것이 아니라 WeaponPoseSlots 를 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|PoseContract")
	TArray<EVBWeaponType> CrouchPoseSlots;

	// 메시 제어를 점유한 기능의 미러와 IK 입력.

	// 사망 여부(State.Dead 미러). ABP 가 사망 포즈에 해로운 OffsetRootBone 과 LegIK 를 끄는 데 쓴다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsDead = false;

	// 렛지 매달림 여부(트래버설 미러). 매달림 중엔 OffsetRootBone 을 Release 하고 LegIK 를 끈다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsHanging = false;

	// 옆걸음 축 -1(왼쪽)~+1(오른쪽). BS_VB_LedgeHang 의 X 에 물린다. 매달림이 아니면 0.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float HangMoveAxis = 0.0f;

	// 자유 등반. 매달림과 같은 이유로 OffsetRootBone 을 Release 로 두고 LegIK 를 끈다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	bool bIsClimbing = false;

	// 등반 블렌드스페이스의 두 축. X=-1(왼쪽)~+1(오른쪽), Y=-1(아래)~+1(위). 등반이 아니면 0.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float ClimbMoveAxisX = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float ClimbMoveAxisY = 0.0f;


	// 매달림 손발 IK 목표(월드, 트래버설 미러). 애니의 손발이 같은 평면에 없어 손을 렛지로 당긴다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector HangHandTargetL = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector HangHandTargetR = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector HangFootTargetL = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	FVector HangFootTargetR = FVector::ZeroVector;

	// 각 체인의 IK 세기 0~1. 0 이면 그 말단은 애니 그대로다(뻗는 중인 손, 벽을 못 찾은 발).
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float HangHandIKWeightL = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float HangHandIKWeightR = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float HangFootIKWeightL = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float HangFootIKWeightR = 0.0f;

	// 처형 장면 중인가. LegIK 를 끄고 bPoseOwnedByMontage 를 켠다.
	bool bInFinisherScene = false;

	// 몽타주가 메시 제어를 점유하고 있어 로코모션이 끼어들면 안 되는가(사망, 처형 등의 OR).
	// 로코모션은 이것 하나만 묻는다. 새 소유자는 UpdatePoseOverrideState 의 OR 에 더한다.
	bool bPoseOwnedByMontage = false;

	// 사망, 트래버설 때의 OffsetRootBone 모드 값. 노드 핀은 GetOffsetRoot*Mode 를 읽으므로
	// 이 둘은 그래프 바인딩이 없음을 확인할 때까지 남겨 둔 쓰기 전용 값이다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	EOffsetRootBoneMode OffsetRootTranslationMode = EOffsetRootBoneMode::Interpolate;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	EOffsetRootBoneMode OffsetRootRotationMode = EOffsetRootBoneMode::Accumulate;

	// 사망, 처형 장면에 0 - LegIK Alpha 핀에 물린다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation")
	float LegIKAlpha = 1.0f;

	// LegIK Alpha 핀용. 핀은 변수가 아니라 함수만 받는다.
	UFUNCTION(BlueprintPure, Category="VB|Animation")
	float GetLegIKAlpha() const;

private:
	friend class FVBCombatAnimationRecoveryTest;

	// 소유자의 LandedDelegate 핸들러. 이 시점의 GetVelocity() 는 아직 충돌 직전 속도다.
	UFUNCTION()
	void HandleOwnerLanded(const FHitResult& Hit);
	void UnbindOwnerLanded();

	// chooser 가 고른 PSD 를 OutDatabases 에 중복 없이 더한다.
	void CollectDatabases(const UChooserTable* Chooser, TArray<UPoseSearchDatabase*>& OutDatabases) const;

	// ---- 프레임 단계. NativeUpdateAnimation 이 매 프레임 이 순서로 부른다. 순서를 바꾸면 뒤 단계가 낡은 값을 읽는다.
	// 1. 폰 확인 전에 돈다. 래그돌 상태와 연결 그래프를 잡고 InteractionTransform 을 그래프로 복사한다.
	void GatherRagdollAndLinkedGraphs();
	// 2. 이번 프레임 메시 제어를 어느 기능이 점유하는지(사망, 매달림, 등반, 사다리, 처형) 미러하고 IK, OffsetRoot, bPoseOwnedByMontage 를 쓴다.
	void UpdatePoseOverrideState(const APawn* OwnerPawn);
	// 3. 슬라이드 그래프 진입, 이탈, 질주 인계.
	void UpdateSlideHandoff(float DeltaSeconds, const class AVBCharacter* VBCharacter);
	// 4. 가드 로코모션 포즈 사용 여부와 bReleaseGASPRootOffset.
	void UpdateGuardHandoff();
	// 5. 상호작용 손 IK: 처형 IK, 그다음 환경 손 IK. 몽타주 상태를 읽으므로 게임 스레드.
	void UpdateInteractionHandIK(float DeltaSeconds);
	// 6. 프레임 시작에 *_LastFrame 값을 복사하고 bFirePoolInterrupt 를 내린다. 7-10 이 원본을 덮기 전에 돌아야 한다.
	void ShiftLastFrameValues();
	// 7. 속도, 가속, 궤적, 이동 상태, Gait, 각도, 재생 속도 등 이동 값을 계산한다. 모든 폰 공통.
	void DeriveMovementValues(float DeltaSeconds, class ACharacter* Character, class UCharacterMovementComponent* CMC);
	// 8-9 는 플레이어(AVBCharacter)만 돈다.
	// 8. 질주, 무기 풀, 풀 교체 interrupt, 그래프 핀 인덱스, 전투모드.
	void UpdateWeaponPoolAndPins(float DeltaSeconds, class AVBCharacter* VBChar, class UCharacterMovementComponent* CMC);
	// 9. 락온 여부.
	void UpdateLockOnFlag();
	// 10. 마지막. 7-9 결과로 MovementMode / Stance / RotationMode 를 정한다.
	void PublishLocomotionStates();
	// ---- 프레임 단계 끝.

	// 7단계가 부르는 GASP UpdateEssentialValues 이식. 속도, 가속, 트랜스폼 기본 값을 쓴다.
	void UpdateEssentialValues(float DeltaSeconds, class ACharacter* Character, class UCharacterMovementComponent* CMC);

	// 프레임 단계가 부르는 헬퍼. 줄 끝 주석이 주요 출력이다.
	void UpdateDistanceToGround(const class ACharacter* Character); // 낙하 중 아래로 선 추적 -> DistanceToGround (지상 0)
	void UpdateGraphPinIndices(); // LocomotionState, crouch, MM 풀 -> LocomotionStateIndex / CrouchPoseIndex / bUsesStateMachinePath
	// GASP 궤적 예측과 충돌로 Trajectory, FutureVelocity, TrajectoryCollision 을 채운다.
	void UpdateTrajectory(float DeltaSeconds, class ACharacter* Character);
	void UpdateLocomotionAngle(float DeltaSeconds, const class ACharacter* Character); // Velocity 방향 보간 -> LocomotionAngle (OW 입력)
	void UpdatePlayRate(); // 속도/Gait -> WantedPlayRate (SpeedToPlayRateCurve)
	void UpdateMovementStateAndGait(bool bHasMovementInput, bool bSprintActive, EVBGait CharacterGait); // -> MMState/Gait/LastMovingGait
	// 풀 교체 interrupt 판정(수렴 래치 + 발도<->납도 즉시).
	void EvaluatePoolInterrupt(float DeltaSeconds, const class UCharacterMovementComponent* CMC, EVBLocomotionState InLocomotionState);

	// 10단계가 부르는 GASP UpdateStates 이식. MovementMode / Stance / RotationMode 만 쓴다.
	void UpdateStates();

	// 슬라이드 그래프 진입, 이탈, 질주 인계 상태.
	FVBAnimSlideHandoff SlideHandoff;
	// 가드 하체 선택의 혼합 완료 상태.
	FVBAnimGuardHandoff GuardHandoff;
	// MM 풀 교체 interrupt 래치.
	FVBAnimPoolInterruptLatch PoolInterruptLatch;
	// 포즈 핀 번호 찾기와 미등록 경고 기록.
	FVBAnimPoseSlotLookup PoseSlotLookup;

	// 트래버설 앞 렛지 월드 트랜스폼. 1단계가 연결 그래프로 복사하고, 트래버설 채널은 ABP 의 BPI 로 읽는다.
	// ABP 가 BPI_InteractionTransform 을 구현하지 않으면 트래버설 MotionMatch 가 결과를 못 낸다.
	UPROPERTY(BlueprintReadWrite, Category="VB|Animation|MotionMatching", meta=(AllowPrivateAccess="true"))
	FTransform InteractionTransform = FTransform::Identity;
	// 연결 그래프의 InteractionTransform 프로퍼티. 그래프가 바뀔 때 찾는다(null = 없음).
	FStructProperty* LinkedInteractionTransform = nullptr;

	bool bSuppressAttackLocomotion = false;
	// UPROPERTY 필수 - 없으면 GC 가 SelectedAnim 을 회수한다.
	UPROPERTY(Transient)
	FPoseSearchBlueprintResult InteractionResult;

	TWeakObjectPtr<ACharacter> LandedSource;
	// 소유자의 이동 의도 컴포넌트. AVBCharacter 가 아니면 null 이고 7단계는 Run 으로 읽는다.
	TWeakObjectPtr<const class UVBLocomotionIntentComponent> LocomotionIntent;
	// 소유자의 무기 상태, 락온, 트래버설 컴포넌트. AVBCharacter 가 아니면 null.
	TWeakObjectPtr<const class UVBWeaponStateComponent> WeaponState;
	TWeakObjectPtr<const class UVBTargetLockComponent> TargetLock;
	TWeakObjectPtr<const class UVBTraversalComponent> OwnerTraversal;

	// 착지 여부와 착지 속도. HandleOwnerLanded 가 채운다.
	UPROPERTY(BlueprintReadWrite, Category="VB|Animation|MotionMatching", meta=(AllowPrivateAccess="true"))
	bool JustLanded = false;

	UPROPERTY(BlueprintReadWrite, Category="VB|Animation|MotionMatching", meta=(AllowPrivateAccess="true"))
	FVector LandVelocity = FVector::ZeroVector;

	// JustLanded 를 유지할 시간(초). 그동안 착지 애니가 후보로 남아 끝까지 재생된다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching", meta=(ClampMin="0.0"))
	float JustLandedGraceDuration = 0.3f;

	// JustLanded 를 끌 월드 시각. 착지할 때마다 다시 늘린다.
	UPROPERTY(Transient, BlueprintReadOnly, Category="VB|Animation|MotionMatching", meta=(AllowPrivateAccess="true"))
	float JustLandedClearTime = 0.0f;

	// 캡슐이 실제로 카메라를 따라 도는가(CMC 기준). RotationMode enum 과 다를 수 있어 제자리 회전은 이것을 본다.
	bool bCMCStrafeRotation = false;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation")
	float LandPredictionHeight = 150.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation")
	float MoveThreshold = 3.0f;

	EVBGait LastMovingGait = EVBGait::Run;

	// Gait 는 bIsSprinting 멤버가 아니라 이동 의도 컴포넌트의 현재 값으로 정한다.

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float LocomotionAngleInterpSpeed = 15.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Animation|MotionMatching")
	float LocomotionAngleMinSpeed = 50.0f;
};

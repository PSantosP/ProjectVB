// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimExecutionContext.h"   // FAnimUpdateContext - 노드 함수 인자
#include "Animation/AnimNodeReference.h"
#include "Animation/VBInteractionAnimInterface.h"
#include "Animation/VBInteractionIK.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "VBEnemyAnimInstance.generated.h"

class UAnimSequenceBase;
class UAnimMontage;
class UBlendSpace;
class UVBEnemyAnimSet;

/**
 * 적 전용 AnimInstance. 속도, 방향, 이동 여부만 계산하고 클립은 소유 적의 AnimSet 에서 읽는다.
 * 플레이어용 UVBAnimInstance 의 모션매칭 계산을 적이 치르지 않도록 분리했다.
 * 소유 폰이 AVBEnemyBase 가 아니면(에디터 미리보기) 참조 포즈로 선다.
 */
UCLASS()
class VB_API UVBEnemyAnimInstance : public UAnimInstance, public IVBInteractionAnimInterface
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

	// 처형 기상 포즈가 평가된 뒤에 캡슐 보정을 적용하도록 예약한다.
	void QueueFinisherGetUpHandoff(UAnimMontage* Montage, const FTransform& ActorBefore,
		const FTransform& ActorAfter, const FVector& PelvisBefore);
	void CancelFinisherGetUpHandoff();

	UPROPERTY(BlueprintReadOnly, Category="VB|Ragdoll")
	FVBRagdollAnimationState RagdollState;

	UFUNCTION(BlueprintPure, Category="VB|Ragdoll", meta=(BlueprintThreadSafe))
	double GetRagdollElapsedTime() const { return RagdollState.ElapsedSeconds; }

	UFUNCTION(BlueprintPure, Category="VB|Ragdoll", meta=(BlueprintThreadSafe))
	bool IsPoweredRagdollPose() const { return RagdollState.bPowered; }

	UFUNCTION(BlueprintPure, Category="VB|Ragdoll", meta=(BlueprintThreadSafe))
	bool IsNotPoweredRagdollPose() const { return !RagdollState.bPowered; }

	// IVBInteractionAnimInterface. 처형 피해자가 상호작용 결과를 든다.
	virtual const FPoseSearchBlueprintResult& GetInteractionResult() const override { return InteractionResult; }
	virtual void SetInteractionResult(const FPoseSearchBlueprintResult& InResult) override { InteractionResult = InResult; }

	// 상호작용 손 IK. UVBInteractionIKLibrary 만 채운다.
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category="VB|Finisher")
	FVBInteractionIKRuntime InteractionIK;

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FTransform GetInteractionIKLeftTarget() const { return InteractionIK.Left.Target; }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FTransform GetInteractionIKRightTarget() const { return InteractionIK.Right.Target; }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	bool IsInteractionIKEnabled() const { return InteractionIK.bEnabled; }

	// 상호작용 손 IK 의 AnimGraph 핀 바인딩 함수. 워커 스레드에서 불리므로 멤버만 읽는다.
	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FVector GetInteractionIKLeftEffector() const { return InteractionIK.Left.Target.GetLocation(); }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	float GetInteractionIKLeftAlpha() const { return InteractionIK.bEnabled ? InteractionIK.Left.Alpha : 0.0f; }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	FVector GetInteractionIKRightEffector() const { return InteractionIK.Right.Target.GetLocation(); }

	UFUNCTION(BlueprintPure, Category="VB|Finisher", meta=(BlueprintThreadSafe))
	float GetInteractionIKRightAlpha() const { return InteractionIK.bEnabled ? InteractionIK.Right.Alpha : 0.0f; }


	// 그래프 자산 핀 바인딩 대상. 워커 스레드에서 불리므로 멤버만 읽는다.
	UFUNCTION(BlueprintPure, Category="VB|Animation|Enemy", meta=(BlueprintThreadSafe))
	UAnimSequenceBase* GetIdleSequence() const;

	UFUNCTION(BlueprintPure, Category="VB|Animation|Enemy", meta=(BlueprintThreadSafe))
	UBlendSpace* GetLocomotionBlendSpace() const;

	UFUNCTION(BlueprintPure, Category="VB|Animation|Enemy", meta=(BlueprintThreadSafe))
	bool IsMoving() const { return bIsMoving; }

	// 노드 함수(ABP 노드 OnUpdate 슬롯). 함수 핀 바인딩이 Sequence, X, Y 핀에 값을 전달하지 못해 이 경로를 쓴다.
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Enemy", meta=(BlueprintThreadSafe))
	void OnIdleNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	UFUNCTION(BlueprintCallable, Category="VB|Animation|Enemy", meta=(BlueprintThreadSafe))
	void OnLocomotionNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

protected:
	// 그래프 VariableGet 링크 대상. 초기화 때 AnimSet 에서 복사한다.
	UPROPERTY(BlueprintReadOnly, Transient, Category="VB|Animation|Enemy")
	TObjectPtr<UAnimSequenceBase> IdleSequence;

	UPROPERTY(BlueprintReadOnly, Transient, Category="VB|Animation|Enemy")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	// 소유 적의 클립 묶음. 초기화 때 한 번 캐시한다.
	UPROPERTY(Transient)
	TObjectPtr<const UVBEnemyAnimSet> AnimSet;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|Enemy")
	float Speed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|Enemy")
	float Direction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|Enemy")
	bool bIsMoving = false;

	// 아직 미사용. 그래프의 공중 분기용 자리.
	UPROPERTY(BlueprintReadOnly, Category="VB|Animation|Enemy")
	bool bIsInAir = false;

private:
	TWeakObjectPtr<UAnimMontage> PendingFinisherGetUp;
	FTransform FinisherActorBefore = FTransform::Identity;
	FTransform FinisherActorAfter = FTransform::Identity;
	FVector FinisherPelvisBefore = FVector::ZeroVector;
	bool bFinisherHandoffReady = false;

	// 상호작용 결과. GC 참조 때문에 UPROPERTY 가 필요하다.
	UPROPERTY(Transient)
	FPoseSearchBlueprintResult InteractionResult;

	// AnimSet 값의 사본. 워커 스레드의 게터가 AnimSet 포인터를 거치지 않게 한다.
	float MovingSpeedThreshold = 10.0f;

	// 계측(vb.EnemyAnim.Telemetry 1 일 때만). 계측 로그 누적 시간, 게임스레드 전용.
	float TelemetryAccum = 0.0f;

	// 노드 함수 호출 수. 워커 스레드에서 늘고 게임스레드에서 읽는다(대략값).
	int32 IdleNodeUpdateCount = 0;
	int32 LocomotionNodeUpdateCount = 0;

	// 노드가 실제로 보는 값. 그래프 밖에서 넣은 값과 비교하려고 둔다.
	FVector LocoNodePosition = FVector::ZeroVector;
	FString LocoNodeAsset;
	FString IdleNodeAsset;
	float IdleNodePlayRate = -1.0f;
	float IdleNodeTime = -1.0f;
	float LocoNodePlayRate = -1.0f;
	bool bLocoNodeLoop = false;
	float LocoNodeTime = -1.0f;
	float LocoNodeDelta = -1.0f; // 노드 함수가 받는 업데이트 컨텍스트의 DeltaTime - 0 이면 재생이 못 나간다
	float LocoNodeWeight = -1.0f;
};

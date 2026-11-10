#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/VBLadderIKContact.h"
#include "AnimNode_VBLadderIK.generated.h"

class UAnimInstance;
class UAnimMontage;

// 현재 입력 포즈에서 접촉을 판정하고 같은 평가 안에서 사다리 목표를 푼다.
USTRUCT(BlueprintInternalUseOnly)
struct VB_API FAnimNode_VBLadderIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	FAnimNode_VBLadderIK();

	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0"))
	float PlantSpeed = 15.0f;
	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0"))
	float ReleaseSpeed = 40.0f;
	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0"))
	float PlantDwellTime = 0.06f;
	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0.001"))
	float ContactBlendTime = 0.08f;
	// 손은 다음 봉으로 이동하는 축 속도를 본다. 접근 끝에는 정지 대기 없이 블렌드를 시작한다.
	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0"))
	float HandApproachAxisSpeed = 30.0f;
	// 고정 뒤 이 거리(cm)를 실제로 이동해야 축 속도에 의한 해제를 허용한다.
	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0"))
	float HandReleaseTravel = 6.0f;
	// UEFN 참조 자세의 ball 높이 약 3.14cm. 발볼 뼈를 봉 속에 넣지 않고 발바닥을 올린다.
	UPROPERTY(EditAnywhere, Category="VB|Contact", meta=(ClampMin="0"))
	float FootSoleOffset = 3.14f;

	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms) override;

	// 지금 평가되는 사다리 몽타주의 배속(게임 스레드). 인스턴스 배속 x 몽타주 RateScale x 그 위치 구간의 배속.
	static FVBLadderIKClipRate ReadClipRate(const UAnimInstance* Anim, const UAnimMontage* Montage);

private:
	struct FLimbFrame;

	// 말단 하나를 평가한다. 남은 접촉이나 보정이 있으면 true.
	bool EvaluateLimb(FComponentSpacePoseContext& Output, int32 Index, float Dt, int32 Telemetry, bool bSample,
		TArray<FBoneTransform>& OutBoneTransforms);
	// 입력 포즈에서 접촉점과 기본 보정을 읽는다.
	void SampleLimbContact(FComponentSpacePoseContext& Output, const FTransform& MeshToWorld, FLimbFrame& Limb);
	// 공백이나 순간이동이면 접촉을 비우고, 속도와 클립 배속 문턱을 구한다.
	void MeasureLimbMotion(FLimbFrame& Limb);
	// 접촉 상태와 올라서기, 줄이기 가중치를 이번 평가만큼 진행한다.
	void StepLimbContact(FLimbFrame& Limb);
	// 보정을 두 뼈 IK 로 풀어 뼈 트랜스폼을 낸다.
	void SolveLimb(FComponentSpacePoseContext& Output, const FTransform& MeshToWorld, const FLimbFrame& Limb,
		TArray<FBoneTransform>& OutBoneTransforms);
	// 말단 하나의 텔레메트리 한 줄. bLog 가 꺼져 있으면 아무것도 안 한다.
	void LogLimb(const FLimbFrame& Limb, const FTransform& MeshToWorld, const FVector& SolverContactWS,
		const FVector& RequestedCorrectionWS) const;

	FBoneReference Ends[4];
	FBoneReference Contacts[4];
	FCompactPoseBoneIndex Middles[4];
	FCompactPoseBoneIndex Roots[4];
	FVBLadderIKContact States[4];
	FVBLadderIKGeometry Geometry;
	FVBLadderIKClipRate ClipRate;
	// 지지 출처(사다리 위 / 올라서기 지지 / 남은 보정 줄이기). PreUpdate 가 정하고 평가가 따른다.
	EVBLadderIKMode Mode = EVBLadderIKMode::Off;
	// 발마다의 기본 보정 가중치(올라서기, 줄이기). 인덱스 2, 3 만 쓴다.
	FVBLadderIKSupportBase SupportBase[4];
	// 사다리 위 마지막 평가에서 손에 요청한 보정(알파 전 - 얼린 값도 같은 알파를 거쳐 다시 요청되므로 이어진다), 그리고 그것을 얼린 것.
	FVector LastHandCorrectionWS[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVBLadderIKHandHold HandHold[2];
	bool bHasGeometry = false;
	// 지난 평가 뒤 남은 접촉, 기본 보정이 있나. 다음 PreUpdate 가 이것으로 Fade 와 Off 를 가른다.
	bool bResidual = false;
	bool bResetStates = false;
	uint32 LadderId = 0;
	uint32 EvaluatedLadderId = 0;
	float Elapsed = 0.0f;
	float MaxCorrection = 0.0f;
	float FootMaxCorrection = 0.0f;
	FVector FootPoseOffsetWS = FVector::ZeroVector;
	float PoseBlendTime = 0.0f;
	float PoseAlpha = 0.0f;
	float HandWeight = 0.0f;
	FVector HandGripOffsets[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float FootWeight = 0.0f;
	bool bEnabled = false;
	bool bWarnedInvalidRate = false;
	// 진단 전용 스냅샷. 병렬 평가에서는 UObject에 접근하지 않는다.
	FString DiagnosticActor;
	FString DiagnosticMontage;
	float DiagnosticMontagePosition = 0.0f;
	float DiagnosticWorldTime = 0.0f;
	float DiagnosticElapsed = 0.0f;
	uint64 DiagnosticFrame = 0;
	uint64 DiagnosticEvaluation = 0;
};

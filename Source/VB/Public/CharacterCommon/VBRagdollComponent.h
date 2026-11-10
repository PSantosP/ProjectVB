// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBRagdollConfig.h"
#include "VBRagdollComponent.generated.h"

class UVBRagdollConfig;
class UPhysicsControlComponent;
class USkeletalMeshComponent;

/** 애니메이션의 ragdoll pose/chooser 입력. 게임 스레드에서 AnimInstance로 복사해서 사용한다. */
USTRUCT(BlueprintType)
struct FVBRagdollAnimationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bPowered = false;
	UPROPERTY(BlueprintReadOnly) EVBRagdollPhysicsProfile PhysicsProfile = EVBRagdollPhysicsProfile::None;
	UPROPERTY(BlueprintReadOnly) FVector SpineVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector CenterOfMassVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float Speed = 0.0f;
	UPROPERTY(BlueprintReadOnly) double TimeToImpact = 0.0;
	UPROPERTY(BlueprintReadOnly) double ElapsedSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly) double RollAmount = 0.0;
	UPROPERTY(BlueprintReadOnly) EVBRagdollInjuryState InjuryState = EVBRagdollInjuryState::None;
	// RagdollTrace 소켓 공간 yaw/pitch, degrees.
	UPROPERTY(BlueprintReadOnly) FVector2D ImpactDirection = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector RollForce = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) bool bHasAllStrengthCurves = false;
};

/** Physics Control만 소유한다. 래그돌 수명, 캡슐 추종, 복제, passive 전환 시점은 UVBPlayerRagdollComponent 와 UVBEnemyRagdollComponent 가 맡는다. 사망 판정은 체력 컴포넌트가 한다. */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBRagdollComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBRagdollComponent();

	// Mesh의 물리 상태/충돌 설정 후 호출. 실패하면 로그와 false로 알리고 passive 정책은 호출자가 결정한다.
	bool BeginPoweredRagdoll(USkeletalMeshComponent* InMesh, UVBRagdollConfig* InConfig);
	// GASP SetPhysicsProfile 에 해당한다. 회복 중에도 같은 제어를 살려 둔다
	UFUNCTION(BlueprintCallable, Category="VB|Ragdoll")
	bool SetPhysicsProfile(EVBRagdollPhysicsProfile NewProfile);
	void EndPoweredRagdoll();
	// 물리 스냅샷을 저장한 뒤 ragdoll 포즈를 멈추고 한 프레임 animation target velocity 를 누른다(기본값은 mesh 평가 뒤 돌아온다)
	bool PrepareForGetUp();
	bool CompleteGetUpProfile();
	FVector GetTargetOrientation() const;
	bool ShouldPlayRollingGetUp() const;

	// Capsule floor probe 또는 전용 ragdoll floor probe 결과를 전달한다. CMC의 오래된 CurrentFloor는 금지.
	void SetEnvironment(const FVector& InFloorNormal, bool bInHasFloor, const FVector& InMoveInput = FVector::ZeroVector);

	UFUNCTION(BlueprintPure, Category="VB|Ragdoll")
	FVBRagdollAnimationState GetAnimationState() const { return AnimationState; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool ValidateSetup(FString& OutError) const;
	void UpdateStrengths();
	bool ClearParentSpaceAnimationVelocity();
	void UpdateBehaviors(float DeltaTime);
	void LogRagdollGeometry(const TCHAR* Phase) const;
	void SampleFinalRagdollPose();
	bool SetCrossLegCollision(bool bEnable);
	bool GetLegBodies(TArray<FName>& OutLeft, TArray<FName>& OutRight) const;
	void ReleaseMeshRefresh();

	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Mesh;
	// 동력 래그돌, 일어나기 동안 잡아 둔 뼈 갱신 요청(FVBMeshRefresh) - 화면 밖 정책이 몽타주만(OnlyTickMontages*)이면 그래프가 멈춰 일어나기가 끝나지 않으므로 정책과 무관하게 잡는다.
	TWeakObjectPtr<USkeletalMeshComponent> RefreshedMesh;
	UPROPERTY(Transient) TObjectPtr<UVBRagdollConfig> Config;
	UPROPERTY(Transient) TObjectPtr<UPhysicsControlComponent> PhysicsControl;
	UPROPERTY(Transient) FVBRagdollAnimationState AnimationState;
	FVector PreviousCenterOfMass = FVector::ZeroVector;
	FVector AutoRollForce = FVector::ZeroVector;
	FVector FloorNormal = FVector::UpVector;
	FVector MoveInput = FVector::ZeroVector;
	bool bHasFloor = false;
	bool bReportedMissingCurves = false;
	int16 AnimationUpdateCounterAtStart = INDEX_NONE;
	bool bHasUpdatedPoseSinceStart = false;
	bool bPendingGetUpProfile = false;
	int16 GetUpAnimationUpdateCounter = INDEX_NONE;
	TOptional<TEnumAsByte<EPhysicsTransformUpdateMode::Type>> OriginalPhysicsTransformUpdateMode;
	// 위 모드에서는 IsSimulatingPhysics() 가 거짓이라 CMC 네트워크 보정이 시뮬 중인 메시를 옮긴다 - 그동안 끄고 끝나면 되돌린다.
	TOptional<ENetworkSmoothingMode> OriginalNetworkSmoothingMode;
	FTransform InitialMeshRelativeTransform = FTransform::Identity;
	int32 GeometryLogSamples = 0;
	FDelegateHandle FinalPoseDiagnosticHandle;
	TArray<FVector> PreviousDiagnosticBonePositions;
	FVector PreviousDiagnosticActorLocation = FVector::ZeroVector;
	FQuat PreviousDiagnosticActorRotation = FQuat::Identity;
	uint64 LastDiagnosticFrame = MAX_uint64;
	int32 DiagnosticPoseSamples = 0;
	int32 DiagnosticWarnings = 0;
	double PeakLocalBoneDelta = 0.0;
	double PeakBoneStep = 0.0;
	double PeakBodySpeed = 0.0;
	FName PeakLocalBone;
	FName PeakStepBone;
	FName PeakSpeedBody;
};

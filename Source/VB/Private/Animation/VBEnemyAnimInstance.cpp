// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBEnemyAnimInstance.h"
#include "Animation/VBInteractionIK.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBEnemyAnimSet.h"
#include "Data/VBEnemyConfig.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Enemy/VBEnemyBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VB/VB.h"
#include "SequencePlayerLibrary.h"
#include "BlendSpacePlayerLibrary.h"

// 적 애니 계측. 1 이면 적마다 2초 간격으로 그래프 밖 값과 노드 안 값을 나란히 찍는다.
// 그래프 핀 값은 정적으로 읽을 수 없어 이 로그가 유일한 대조 수단이다.
static TAutoConsoleVariable<int32> CVarEnemyAnimTelemetry(
	TEXT("vb.EnemyAnim.Telemetry"),
	0,
	TEXT("Enemy AnimInstance telemetry: 0=off, 1=log graph inputs and asset-player state every 2s per enemy."),
	ECVF_Default
);

void UVBEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CancelFinisherGetUpHandoff();

	AnimSet = nullptr;
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(TryGetPawnOwner());
	if (!Enemy)
	{
		// 에디터 미리보기, CDO 에는 폰이 없다. 정상.
		return;
	}

	// GetEnemyConfig() 는 미할당이면 CDO 를 돌려주므로 null 이 아니다. AnimSet 은 CDO 에서 비어 있다.
	AnimSet = Enemy->GetEnemyConfig()->AnimSet;
	if (!AnimSet)
	{
		// 참조 포즈로 서게 되므로 원인을 경고로 남긴다.
		VB_LOG(Warning, "%s: EnemyConfig 에 AnimSet 이 없다 -> 클립 없이 참조 포즈. DA_EnemyAnimSet_* 을 할당하라",
			*GetNameSafe(Enemy));
		return;
	}

	MovingSpeedThreshold = AnimSet->MovingSpeedThreshold;
	IdleSequence = AnimSet->Idle;
	LocomotionBlendSpace = AnimSet->Locomotion;

}

void UVBEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// 몽타주 평가 데이터는 이 함수 전에 갱신되므로, 나중에 예약된 인계는 다음 업데이트를 기다린다.
	bFinisherHandoffReady = PendingFinisherGetUp.IsValid() && Montage_IsPlaying(PendingFinisherGetUp.Get());

	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
	{
		return;
	}

	// 상호작용 손 IK. 상호작용 중이 아니면 꺼진 상태로 돌아온다.
	UVBInteractionIKLibrary::Update(this, GetInteractionResult(), DeltaSeconds, InteractionIK);
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Pawn);
	const UVBRagdollComponent* Ragdoll = Enemy ? Enemy->FindComponentByClass<UVBRagdollComponent>() : nullptr;
	RagdollState = Ragdoll ? Ragdoll->GetAnimationState() : FVBRagdollAnimationState();
	const float StrideScale = VBStrideScale::Resolve(Pawn);

	const FVector Velocity = Pawn->GetVelocity();
	const FVector Velocity2D = FVector(Velocity.X, Velocity.Y, 0.0);
	const float GroundSpeed = static_cast<float>(Velocity2D.Size());

	// 메시가 크면 같은 클립의 보폭도 커지므로 지면 속도를 배율로 나눠 다리와 이동을 맞춘다.
	Speed = GroundSpeed / StrideScale;

	// 이동 여부는 실제 지면 속도로 잰다(배율 무관).
	bIsMoving = GroundSpeed > MovingSpeedThreshold;

	// 방향 = 액터 정면 기준 속도 각도. 폐기된 CalculateDirection 대신 같은 식을 직접 쓴다.
	if (bIsMoving)
	{
		const FVector Forward = Pawn->GetActorForwardVector();
		const FVector Right = Pawn->GetActorRightVector();
		const FVector Dir = Velocity2D.GetSafeNormal();
		const double ForwardDot = FVector::DotProduct(Forward, Dir);
		const double RightDot = FVector::DotProduct(Right, Dir);
		Direction = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot)));
	}
	// 멈추면 마지막 방향을 유지한다 - 0 으로 튀면 앞걸음으로 스냅한다.

	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		bIsInAir = Move && Move->IsFalling();
	}

	// 계측(2초 간격, vb.EnemyAnim.Telemetry).
	if (CVarEnemyAnimTelemetry.GetValueOnGameThread() == 0)
	{
		return;
	}

	TelemetryAccum += DeltaSeconds;
	if (TelemetryAccum >= 2.0f)
	{
		TelemetryAccum = 0.0f;
		VB_LOG(Log, "[EnemyAnim] %s speed=%.0f dir=%.0f moving=%d idle=%s loco=%s montage=%s nodeFn(idle/loco)=%d/%d nodeSees idle=%s bs=%s pos=(%.0f,%.0f)",
			*GetNameSafe(Pawn), Speed, Direction, bIsMoving ? 1 : 0,
			*GetNameSafe(GetIdleSequence()), *GetNameSafe(GetLocomotionBlendSpace()),
			*GetNameSafe(GetCurrentActiveMontage()), IdleNodeUpdateCount, LocomotionNodeUpdateCount,
			*IdleNodeAsset, *LocoNodeAsset, LocoNodePosition.X, LocoNodePosition.Y);
		VB_LOG(Log, "[EnemyAnim]   idleNode rate=%.2f time=%.2f | locoNode rate=%.2f loop=%d time=%.2f delta=%.4f weight=%.2f",
			IdleNodePlayRate, IdleNodeTime, LocoNodePlayRate, bLocoNodeLoop ? 1 : 0, LocoNodeTime, LocoNodeDelta, LocoNodeWeight);
	}
}

void UVBEnemyAnimInstance::QueueFinisherGetUpHandoff(UAnimMontage* Montage, const FTransform& ActorBefore,
	const FTransform& ActorAfter, const FVector& PelvisBefore)
{
	PendingFinisherGetUp = Montage;
	FinisherActorBefore = ActorBefore;
	FinisherActorAfter = ActorAfter;
	FinisherPelvisBefore = PelvisBefore;
	bFinisherHandoffReady = false;
}

void UVBEnemyAnimInstance::CancelFinisherGetUpHandoff()
{
	PendingFinisherGetUp.Reset();
	bFinisherHandoffReady = false;
}

void UVBEnemyAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	if (!bFinisherHandoffReady)
	{
		return;
	}

	AActor* Actor = GetOwningActor();
	USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const bool bCanApply = Actor && Actor->HasAuthority() && Mesh && !Mesh->IsAnySimulatingPhysics() &&
		PendingFinisherGetUp.IsValid() && Montage_IsPlaying(PendingFinisherGetUp.Get());
	CancelFinisherGetUpHandoff();
	if (!bCanApply)
	{
		return;
	}

	// 예약 이후 소비된 루트모션을 보정된 기준에서 유지한다.
	const FTransform CorrectedActor = Actor->GetActorTransform().GetRelativeTransform(FinisherActorBefore) * FinisherActorAfter;
	const FVector BeforeLocation = Actor->GetActorLocation();
	Actor->SetActorLocationAndRotation(CorrectedActor.GetLocation(), CorrectedActor.GetRotation(), false, nullptr,
		ETeleportType::TeleportPhysics);
	Actor->ForceNetUpdate();

	// PostEvaluate 는 버퍼 교체 전이라 GetBoneLocation 대신 새로 평가된 버퍼를 읽는다.
	const int32 PelvisIndex = Mesh->GetBoneIndex(TEXT("pelvis"));
	const TArray<FTransform>& NewPose = Mesh->GetEditableComponentSpaceTransforms();
	if (NewPose.IsValidIndex(PelvisIndex))
	{
		const FVector PelvisAfter = Mesh->GetComponentTransform().TransformPosition(NewPose[PelvisIndex].GetLocation());
		VB_LOG(Log, "FinisherGetUp handoff: actor=%s frame=%llu capsuleStepCm=%.2f pelvisStepCm=%.2f",
			*Actor->GetName(), GFrameCounter, FVector::Distance(BeforeLocation, Actor->GetActorLocation()),
			FVector::Distance(FinisherPelvisBefore, PelvisAfter));
	}
}

UAnimSequenceBase* UVBEnemyAnimInstance::GetIdleSequence() const
{
	return IdleSequence;
}

UBlendSpace* UVBEnemyAnimInstance::GetLocomotionBlendSpace() const
{
	return LocomotionBlendSpace;
}

void UVBEnemyAnimInstance::OnIdleNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	// 매 업데이트 같은 자산을 넣는다. 같은 값이면 라이브러리가 무시한다.
	EAnimNodeReferenceConversionResult Result;
	const FSequencePlayerReference Player = USequencePlayerLibrary::ConvertToSequencePlayer(Node, Result);
	if (Result == EAnimNodeReferenceConversionResult::Succeeded)
	{
		USequencePlayerLibrary::SetSequence(Player, IdleSequence);
		if (CVarEnemyAnimTelemetry.GetValueOnAnyThread() == 0)
		{
			return;
		}

		++IdleNodeUpdateCount;
		UAnimSequenceBase* NodeSequence = nullptr;
		USequencePlayerLibrary::GetSequence(Player, NodeSequence);
		IdleNodeAsset = GetNameSafe(NodeSequence);
		IdleNodePlayRate = USequencePlayerLibrary::GetPlayRate(Player);
		IdleNodeTime = USequencePlayerLibrary::GetAccumulatedTime(Player);
	}
}

void UVBEnemyAnimInstance::OnLocomotionNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	EAnimNodeReferenceConversionResult Result;
	const FBlendSpacePlayerReference Player = UBlendSpacePlayerLibrary::ConvertToBlendSpacePlayer(Node, Result);
	if (Result == EAnimNodeReferenceConversionResult::Succeeded)
	{
		UBlendSpacePlayerLibrary::SetBlendSpace(Player, LocomotionBlendSpace);
		if (CVarEnemyAnimTelemetry.GetValueOnAnyThread() == 0)
		{
			return;
		}

		++LocomotionNodeUpdateCount;
		LocoNodeAsset = GetNameSafe(UBlendSpacePlayerLibrary::GetBlendSpace(Player));
		LocoNodePosition = UBlendSpacePlayerLibrary::GetPosition(Player);
		LocoNodePlayRate = UBlendSpacePlayerLibrary::GetPlayRate(Player);
		bLocoNodeLoop = UBlendSpacePlayerLibrary::GetLoop(Player);
		LocoNodeTime = UBlendSpacePlayerLibrary::GetAccumulatedTime(Player);
		if (const FAnimationUpdateContext* Ctx = Context.GetContext())
		{
			LocoNodeDelta = Ctx->GetDeltaTime();
			LocoNodeWeight = Ctx->GetFinalBlendWeight();
		}
	}
}

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBRagdollBody.h"

#include "Animation/AnimInstance.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "CollisionQueryParams.h"  // SCENE_QUERY_STAT
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"  // 바디의 뼈 이름
#include "PoseSearch/PoseSearchLibrary.h"
#include "VB/VB.h"

void FVBRagdollCollisionState::Capture(const UPrimitiveComponent& Component)
{
	Profile = Component.GetCollisionProfileName();
	Enabled = Component.GetCollisionEnabled();
	ObjectType = Component.GetCollisionObjectType();
	Responses = Component.GetCollisionResponseToChannels();
}

void FVBRagdollCollisionState::Apply(UPrimitiveComponent& Component) const
{
	Component.SetCollisionProfileName(Profile);
	Component.SetCollisionObjectType(ObjectType);
	Component.SetCollisionResponseToChannels(Responses);
	Component.SetCollisionEnabled(Enabled);
}

FName VBRagdollBody::ResolveFollowBone(const USkeletalMeshComponent& Mesh, FName ConfiguredBone, FName RagdollRootBone)
{
	// 설정 -> 래그돌 뿌리 -> pelvis(GASP 와 UE 마네킹 골격이 쓰는 이름).
	TArray<FName, TInlineAllocator<3>> Candidates;
	if (!ConfiguredBone.IsNone())
	{
		Candidates.Add(ConfiguredBone);
	}

	if (!RagdollRootBone.IsNone())
	{
		Candidates.Add(RagdollRootBone);
	}

	Candidates.Add(TEXT("pelvis"));

	for (const FName& Candidate : Candidates)
	{
		if (Mesh.GetBoneIndex(Candidate) != INDEX_NONE)
		{
			return Candidate;
		}
	}

	return NAME_None;
}

void VBRagdollBody::ConfigureCapsule(UCapsuleComponent& Capsule)
{
	// 캡슐을 스윕이 통하는 상태로 돌린다 - 월드에만 막히고 폰은 통과(시체가 플레이어를 밀지 않게), QueryOnly(물리 충돌은 래그돌이 든다).
	Capsule.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule.SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule.SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Capsule.SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
}

void VBRagdollBody::SaveGetUpPose(UAnimInstance& Anim)
{
	Anim.SavePoseSnapshot(TEXT("Ragdoll"));
	UPoseSearchLibrary::OverridePoseHistoryFromOwningMesh(&Anim, TEXT("PoseHistory"));
}

FVBRagdollGround VBRagdollBody::FindGround(const AActor& Body, const UCapsuleComponent& Capsule, const USkeletalMeshComponent* Mesh,
	const FVector& BoneLocation)
{
	// 캡슐 바닥을 땅에 세운다. 뼈 위치를 그대로 쓰면 캡슐 중심이 골반 높이라 아래 절반이 땅에 묻힌다.
	// GASP 는 여기서 SphereTraceSingle 로 바닥을 찾아 그 지점을 쓰고, 안 맞으면 뼈 위치를 그대로 쓴다.
	const float HalfHeight = Capsule.GetScaledCapsuleHalfHeight();
	const float Radius = Capsule.GetScaledCapsuleRadius();

	FVBRagdollGround Ground;
	Ground.TargetLocation = BoneLocation;

	FHitResult GroundHit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VBDrivenRagdollGround), false, &Body);
	TraceParams.AddIgnoredComponent(Mesh);
	const FVector TraceEnd = BoneLocation - FVector(0.0f, 0.0f, HalfHeight * 2.0f);
	const UWorld* World = Body.GetWorld();
	if (World && World->SweepSingleByChannel(
			GroundHit, BoneLocation, TraceEnd, FQuat::Identity, ECC_WorldStatic,
			FCollisionShape::MakeSphere(Radius), TraceParams))
	{
		Ground.TargetLocation.Z = GroundHit.ImpactPoint.Z + HalfHeight;
		Ground.Normal = GroundHit.ImpactNormal;
		Ground.bHasFloor = true;
	}

	return Ground;
}

FRotator VBRagdollBody::ResolveRotation(const UVBRagdollComponent* RecoverableRagdoll, bool bFollowBoneYaw,
	const FTransform& BoneTransform, const FRotator& CapsuleRotation)
{
	return RecoverableRagdoll
		? RecoverableRagdoll->GetTargetOrientation().Rotation()
		: bFollowBoneYaw ? FRotator(0.0f, BoneTransform.Rotator().Yaw, 0.0f)
		: CapsuleRotation;
}

void VBRagdollBody::MoveCapsule(ACharacter& Body, FName FollowBone, const FVector& MoveDelta,
	const FRotator& TargetRotation, double Now)
{
	UCapsuleComponent* Capsule = Body.GetCapsuleComponent();
	USkeletalMeshComponent* MeshComp = Body.GetMesh();
	UCharacterMovementComponent* Movement = Body.GetCharacterMovement();
	const FQuat StartRotation = Capsule->GetComponentQuat();
	static const IConsoleVariable* PoseDiagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Ragdoll.PoseDiagnostics"));
	const FBodyInstance* FollowBody = PoseDiagnostics && PoseDiagnostics->GetInt() != 0
		? MeshComp->GetBodyInstance(FollowBone) : nullptr;
	const FTransform BodyBeforeFollow = FollowBody ? FollowBody->GetUnrealWorldTransform() : FTransform::Identity;
	const FTransform MeshRelativeBeforeFollow = MeshComp->GetRelativeTransform();
	FHitResult MoveHit(1.0f);
	{
		// SafeMove 는 새 yaw 까지 TeleportPhysics 로 관통을 풀 수 있다.
		// 그 부모 teleport 가 따로 시뮬레이션하는 mesh body 를 옮기지 않게 막는다.
		const FScopedPreventAttachedComponentMove PreventMeshMove(MeshComp);
		if (Movement)
		{
			Movement->SafeMoveUpdatedComponent(MoveDelta, TargetRotation, true, MoveHit);
		}
		else
		{
			Capsule->MoveComponent(MoveDelta, TargetRotation, true, &MoveHit);
		}
	}
	// mesh 평가 전에 body 를 teleport 하지 않고 component frame 을 되돌린다(scope 만으로는 상대 drift 가 쌓인다).
	// 값을 직접 넣고 UpdateComponentToWorld - SetRelativeTransform 은 Teleport 경고를, TeleportPhysics 는 바디를 튀게 한다.
	MeshComp->SetRelativeTransform_Direct(MeshRelativeBeforeFollow);
	MeshComp->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::None);
	if (FollowBody)
	{
		const FTransform BodyAfterFollow = FollowBody->GetUnrealWorldTransform();
		const double BodyStep = FVector::Distance(BodyBeforeFollow.GetLocation(), BodyAfterFollow.GetLocation());
		const double BodyRotationStep = FMath::RadiansToDegrees(
			BodyBeforeFollow.GetRotation().AngularDistance(BodyAfterFollow.GetRotation()));
		const double ActorRotationStep = FMath::RadiansToDegrees(StartRotation.AngularDistance(Capsule->GetComponentQuat()));
		if (ActorRotationStep > 45.0 || BodyStep > 1.0 || BodyRotationStep > 1.0)
		{
			VB_LOG(Log, "RagdollFollowFrame: %s frame=%llu worldTime=%.3f actorRotationStepDeg=%.2f "
				"body=%s bodyStepCm=%.3f bodyRotationStepDeg=%.3f finalPenetrating=%d",
				*Body.GetName(), GFrameCounter, Now, ActorRotationStep, *FollowBone.ToString(), BodyStep, BodyRotationStep,
				MoveHit.bStartPenetrating ? 1 : 0);
		}
	}
}

FVBFastestBody VBRagdollBody::FindFastestBody(const USkeletalMeshComponent& Mesh)
{
	FVBFastestBody Fastest;
	for (const FBodyInstance* Body : Mesh.Bodies)
	{
		if (!Body)
		{
			continue;
		}

		const float Speed = static_cast<float>(Body->GetUnrealWorldVelocity().Size());
		// NaN 속도도 가장 빠른 값으로 친다 - 물리가 깨진 바디를 멈췄다고 판정하지 않게 한다.
		if (!(Speed <= Fastest.Speed))
		{
			Fastest.Speed = Speed;
			Fastest.Bone = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
		}
	}

	return Fastest;
}

int32 VBRagdollBody::NextSettledFrames(int32 SettledFrames, float MaxBodySpeed, float SettleSpeed)
{
	return MaxBodySpeed < SettleSpeed ? SettledFrames + 1 : 0;
}

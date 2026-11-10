// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class AActor;
class ACharacter;
class UAnimInstance;
class UCapsuleComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UVBRagdollComponent;

// 래그돌 전의 충돌 설정. 들어갈 때 잡아 두고 나올 때 되돌린다.
struct VB_API FVBRagdollCollisionState
{
	FName Profile;
	TEnumAsByte<ECollisionEnabled::Type> Enabled = ECollisionEnabled::NoCollision;
	TEnumAsByte<ECollisionChannel> ObjectType = ECC_WorldStatic;
	FCollisionResponseContainer Responses;

	void Capture(const UPrimitiveComponent& Component);
	void Apply(UPrimitiveComponent& Component) const;
};

// 뼈 아래 바닥을 찾은 결과.
struct FVBRagdollGround
{
	FVector TargetLocation = FVector::ZeroVector; // 캡슐 중심이 갈 자리
	FVector Normal = FVector::UpVector;
	bool bHasFloor = false;
};

// 지금 가장 빠른 물리 바디.
struct FVBFastestBody
{
	float Speed = 0.0f;
	FName Bone = NAME_None;
};

// 플레이어와 적이 함께 쓰는, 래그돌 몸을 다루는 상태 없는 함수들이다. 수명과 복제는 각자의 주인이 든다.
namespace VBRagdollBody
{
	// 캡슐을 월드에만 막히는 질의 전용으로 바꾼다.
	VB_API void ConfigureCapsule(UCapsuleComponent& Capsule);
	// 캡슐만 스윕으로 옮기고 시뮬레이션 중인 메시는 제자리에 둔다.
	VB_API void MoveCapsule(ACharacter& Body, FName FollowBone, const FVector& MoveDelta, const FRotator& TargetRotation, double Now);
	// 일어나기 전에 지금 물리 자세를 스냅샷과 PoseHistory 에 남긴다.
	VB_API void SaveGetUpPose(UAnimInstance& Anim);
	// 설정 뼈, 래그돌 뿌리, pelvis 순서로 골격에 있는 첫 뼈를 돌려준다. 없으면 None 이다.
	VB_API FName ResolveFollowBone(const USkeletalMeshComponent& Mesh, FName ConfiguredBone, FName RagdollRootBone);
	// 뼈 아래 바닥을 찾아 캡슐이 갈 자리를 돌려준다. 바닥이 없으면 뼈 위치 그대로다.
	VB_API FVBRagdollGround FindGround(const AActor& Body, const UCapsuleComponent& Capsule, const USkeletalMeshComponent* Mesh,
		const FVector& BoneLocation);
	// 이번 틱의 캡슐 회전을 고른다. RecoverableRagdoll 은 회복 가능한 쓰러짐일 때만 전달한다.
	VB_API FRotator ResolveRotation(const UVBRagdollComponent* RecoverableRagdoll, bool bFollowBoneYaw,
		const FTransform& BoneTransform, const FRotator& CapsuleRotation);
	// 모든 바디 중 가장 빠른 것을 찾는다. 바디가 없거나 전부 멎었으면 속도 0, 뼈 None 이다.
	VB_API FVBFastestBody FindFastestBody(const USkeletalMeshComponent& Mesh);
	// 문턱 아래면 연속 프레임 수를 하나 올리고, 아니면 0 으로 되돌린다.
	VB_API int32 NextSettledFrames(int32 SettledFrames, float MaxBodySpeed, float SettleSpeed);
}

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBInteractionIK.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchInteractionAsset.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Engine/SkeletalMesh.h"
#include "VB/VB.h"        // VB_LOG

void UVBInteractionIKLibrary::Update(UAnimInstance* AnimInstance,
	const FPoseSearchBlueprintResult& Result,
	float DeltaSeconds,
	FVBInteractionIKRuntime& InOutRuntime)
{
	// 꺼진 상태로 시작해 아래 조건을 다 통과해야 켠다. 빠져나가는 길이 여럿이라 끄기를 빠뜨리지 않게.
	InOutRuntime.bEnabled = false;
	InOutRuntime.Left = FVBInteractionIKHand();
	InOutRuntime.Right = FVBInteractionIKHand();

	const UPoseSearchInteractionAsset* InteractionAsset =
		AnimInstance ? Cast<UPoseSearchInteractionAsset>(Result.SelectedAnim) : nullptr;
	if (!InteractionAsset)
	{
		InOutRuntime.Constraints.Reset();
		return;
	}

	// 내 역할 몽타주가 실제로 돌 때만 IK 를 건다. 결과와 재생의 수명이 다르다.
	UAnimMontage* RoleMontage = Cast<UAnimMontage>(InteractionAsset->GetAnimationAsset(Result.Role));
	if (!RoleMontage || !AnimInstance->Montage_IsActive(RoleMontage))
	{
		InOutRuntime.Constraints.Reset();
		return;
	}

	InOutRuntime.bEnabled = true;

	// 시간은 몽타주에서 읽는다. 결과의 SelectedTime 은 재생이 흐른 만큼 낡는다.
	const float MontageTime = AnimInstance->Montage_GetPosition(RoleMontage);

	// 엔진 API 가 non-const 배열을 요구한다.
	TArray<UObject*> AnimContexts;
	AnimContexts.Reserve(Result.AnimContexts.Num());
	for (const TObjectPtr<const UObject>& Context : Result.AnimContexts)
	{
		AnimContexts.Add(const_cast<UObject*>(Context.Get()));
	}

	// 골격이 스키마와 다르면 엔진이 크래시하므로 짝을 지은 뒤에도 다시 확인한다.
	for (int32 RoleIndex = 0; RoleIndex < InteractionAsset->GetNumRoles(); ++RoleIndex)
	{
		const FName RoleName = InteractionAsset->GetRole(RoleIndex);
		const UAnimInstance* RoleAnim = AnimContexts.IsValidIndex(RoleIndex)
			? Cast<UAnimInstance>(AnimContexts[RoleIndex])
			: nullptr;
		if (!DoesSkeletonMatchSchema(Result.SelectedDatabase, RoleName, RoleAnim))
		{
			VB_LOG(Warning, "InteractionIK: 역할 %s 의 골격이 스키마와 다르다 - 제약 갱신을 건너뛴다 "
				"(그대로 부르면 엔진이 뼈 인덱스로 크래시한다)", *RoleName.ToString());
			InOutRuntime.Constraints.Reset();
			InOutRuntime.bEnabled = false;
			return;
		}
	}

	UPoseSearchInteractionLibrary::UpdateConstraints(
		InOutRuntime.Constraints, DeltaSeconds, InteractionAsset, AnimContexts,
		MontageTime, Result.bIsMirrored, Result.BlendParameters);

	const USkeletalMeshComponent* Mesh = AnimInstance->GetOwningComponent();
	if (!Mesh)
	{
		return;
	}

	const FTransform MeshToWorld = Mesh->GetComponentTransform();

	auto FillHand = [&InOutRuntime, &Result, &MeshToWorld](FName Socket, FVBInteractionIKHand& OutHand)
	{
		float DesiredReach = 0.0f;
		FTransform WorldTarget = FTransform::Identity;
		bool bValid = false;

		UPoseSearchInteractionLibrary::GetConstraint(
			InOutRuntime.Constraints, Result.Role, Socket, DesiredReach, WorldTarget, bValid);

		if (!bValid)
		{
			// 이 소켓에는 제약이 없다(한 손만 잡는 처형). 정상.
			return;
		}

		// 엔진은 월드 변환을 주고 AnimGraph IK 는 컴포넌트 공간에서 일한다.
		OutHand.Target = WorldTarget.GetRelativeTransform(MeshToWorld);
		OutHand.Alpha = FMath::Clamp(DesiredReach, 0.0f, 1.0f);
	};

	FillHand(InOutRuntime.LeftSocket, InOutRuntime.Left);
	FillHand(InOutRuntime.RightSocket, InOutRuntime.Right);
}

bool UVBInteractionIKLibrary::DoesSkeletonMatchSchema(const UPoseSearchDatabase* Database,
	FName Role,
	const UAnimInstance* AnimInstance)
{
	if (!Database || !Database->Schema || !AnimInstance)
	{
		return false;
	}

	const FPoseSearchRoledSkeleton* RoledSkeleton = Database->Schema->GetRoledSkeleton(Role);
	if (!RoledSkeleton || !RoledSkeleton->Skeleton)
	{
		// 스키마가 그 역할을 모른다(역할 이름 오타 또는 다른 용도의 DB).
		return false;
	}

	// 골격은 메시 -> 자산 -> 골격 경로로 읽는다. 버전마다 바뀌지 않는 경로다.
	const USkeletalMeshComponent* Mesh = AnimInstance->GetOwningComponent();
	const USkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = MeshAsset ? MeshAsset->GetSkeleton() : nullptr;

	// 모르면 false.
	return Skeleton != nullptr && Skeleton == RoledSkeleton->Skeleton;
}

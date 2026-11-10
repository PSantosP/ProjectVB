// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimNotifyState_InteractionWarp.h"

#include "Animation/VBInteractionAnimInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"
#include "MotionWarpingAdapter.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "RootMotionModifier.h"
#include "VB/VB.h"        // VB_LOG

URootMotionModifier* UVBAnimNotifyState_InteractionWarp::AddRootMotionModifier_Implementation(
	UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation,
	float StartTime, float EndTime) const
{
	URootMotionModifier* Modifier = Super::AddRootMotionModifier_Implementation(
		MotionWarpingComp, Animation, StartTime, EndTime);

	if (!Modifier)
	{
		return nullptr;
	}

	// 부모는 이미 바인딩돼 있으면 건너뛰므로 먼저 건다. const_cast 는 엔진도 같은 자리에서 한다.
	Modifier->OnActivateDelegate.BindDynamic(
		const_cast<UVBAnimNotifyState_InteractionWarp*>(this),
		&UVBAnimNotifyState_InteractionWarp::HandleWarpActivate);
	Modifier->OnDeactivateDelegate.BindDynamic(
		const_cast<UVBAnimNotifyState_InteractionWarp*>(this),
		&UVBAnimNotifyState_InteractionWarp::HandleWarpDeactivate);

	return Modifier;
}

bool UVBAnimNotifyState_InteractionWarp::CalculateAlignedTransform(const FPoseSearchBlueprintResult& Result,
	float TimeOffset, FName AnchorRole, FTransform& OutTransform)
{
	OutTransform = FTransform::Identity;
	const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(Result.SelectedAnim);
	if (!Asset || !Result.SelectedDatabase || !Result.SelectedDatabase->Schema
		|| !Result.bIsInteraction || !FMath::IsFinite(Result.SelectedTime) || !FMath::IsFinite(TimeOffset)
		|| Result.AnimContexts.Num() != Asset->GetNumRoles()
		|| Result.ActorRootTransforms.Num() != Asset->GetNumRoles()
		|| Result.ActorRootBoneTransforms.Num() != Asset->GetNumRoles()
		|| !Result.AnimContexts.IsValidIndex(Result.RoleIndex))
	{
		return false;
	}
	for (int32 Index = 0; Index < Asset->GetNumRoles(); ++Index)
	{
		if (!Result.AnimContexts[Index] || Result.ActorRootTransforms[Index].ContainsNaN()
			|| Result.ActorRootBoneTransforms[Index].ContainsNaN())
		{
			return false;
		}
	}

	FTransform Aligned;
	UPoseSearchInteractionLibrary::CalculateFullAlignedTransform(Result, TimeOffset, true, Aligned);
	if (!AnchorRole.IsNone())
	{
		int32 AnchorIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Asset->GetNumRoles(); ++Index)
		{
			if (Asset->GetRole(Index) == AnchorRole)
			{
				AnchorIndex = Index;
				break;
			}
		}
		if (AnchorIndex == INDEX_NONE)
		{
			return false;
		}
		FPoseSearchBlueprintResult AnchorResult = Result;
		AnchorResult.Role = AnchorRole;
		AnchorResult.RoleIndex = AnchorIndex;
		FTransform AlignedAnchor;
		UPoseSearchInteractionLibrary::CalculateFullAlignedTransform(AnchorResult, 0.0f, true, AlignedAnchor);
		const FTransform OriginalAnchor = Result.ActorRootBoneTransforms[AnchorIndex] * Result.ActorRootTransforms[AnchorIndex];
		Aligned = Aligned.GetRelativeTransform(AlignedAnchor) * OriginalAnchor;
	}
	if (Aligned.ContainsNaN())
	{
		return false;
	}
	OutTransform = Aligned;
	return true;
}

void UVBAnimNotifyState_InteractionWarp::HandleWarpActivate(UMotionWarpingComponent* MotionWarpingComp,
	URootMotionModifier* Modifier)
{
	if (!MotionWarpingComp || !Modifier)
	{
		return;
	}

	// 목표 이름은 모디파이어가 든다. 여기서 다시 적지 않는다.
	URootMotionModifier_Warp* WarpModifier = Cast<URootMotionModifier_Warp>(Modifier);
	if (!WarpModifier || WarpModifier->WarpTargetName.IsNone())
	{
		VB_LOG(Warning, "InteractionWarp: 모디파이어가 워프형이 아니거나 WarpTargetName 이 비었다 (%s). "
			"노티파이의 RootMotionModifier 설정을 확인하라", *GetNameSafe(Modifier));
		return;
	}

	// 바디 메시는 GetMesh 로 가져온다. FindComponentByClass 는 무기 메시를 집을 수 있다.
	const AActor* Owner = MotionWarpingComp->GetOwner();
	const ACharacter* Character = Cast<ACharacter>(Owner);
	USkeletalMeshComponent* Mesh = Character
		? Character->GetMesh()
		: (Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr);
	const IVBInteractionAnimInterface* AnimInstance =
		Mesh ? Cast<IVBInteractionAnimInterface>(Mesh->GetAnimInstance()) : nullptr;
	if (!AnimInstance)
	{
		// 인터페이스 미구현 AnimInstance 가 재생했다. 설정 오류라 경고한다.
		VB_LOG(Warning, "InteractionWarp: 상호작용 AnimInstance 를 못 찾았다 (owner=%s)", *GetNameSafe(Owner));
		return;
	}

	const FPoseSearchBlueprintResult& Result = AnimInstance->GetInteractionResult();
	if (!Result.bIsInteraction || !Result.SelectedAnim)
	{
		// 상호작용 재생이 아니다(단독 재생). 워프 없이 진행하는 정상 경로.
		return;
	}

	// 창이 끝날 때 있어야 할 자리를 공간 검사와 같은 계산으로 구한다.
	const float TimeOffset = WarpModifier->EndTime - Result.SelectedTime;
	FTransform AlignedXf = FTransform::Identity;
	if (!CalculateAlignedTransform(Result, TimeOffset, AlignmentAnchorRole, AlignedXf))
	{
		VB_LOG(Warning, "InteractionWarp: invalid alignment data (owner=%s anchor=%s)",
			*GetNameSafe(Owner), *AlignmentAnchorRole.ToString());
		return;
	}

	// 위치는 메시 원점(발 높이) 그대로, 회전은 엔진이 메시 오프셋을 다시 곱하므로 액터 공간으로 되돌린다.
	const FTransform DesiredActorXf = Mesh->GetRelativeTransform().Inverse() * AlignedXf;

	MotionWarpingComp->AddOrUpdateWarpTargetFromLocationAndRotation(
		WarpModifier->WarpTargetName, AlignedXf.GetLocation(), DesiredActorXf.Rotator());

	VB_LOG(Log, "InteractionWarp: %s 워프 목표 설정 (역할=%s, 남은=%.2fs, 위치=%s, yaw=%.1f, anchor=%s)",
		*GetNameSafe(Owner), *Result.Role.ToString(), TimeOffset, *AlignedXf.GetLocation().ToCompactString(),
		DesiredActorXf.Rotator().Yaw, *AlignmentAnchorRole.ToString());
	const UMotionWarpingBaseAdapter* Adapter = MotionWarpingComp->GetOwnerAdapter();
	const FVector ActualLocation = Adapter ? Adapter->GetVisualRootLocation() : Mesh->GetComponentLocation();
	VB_LOG(Log, "InteractionWarp start: actor=%s distance2D=%.2fcm actual=%s",
		*GetNameSafe(Owner), FVector::Dist2D(ActualLocation, AlignedXf.GetLocation()),
		*ActualLocation.ToCompactString());
}

void UVBAnimNotifyState_InteractionWarp::HandleWarpDeactivate(UMotionWarpingComponent* MotionWarpingComp,
	URootMotionModifier* Modifier)
{
	const URootMotionModifier_Warp* Warp = Cast<URootMotionModifier_Warp>(Modifier);
	const UMotionWarpingBaseAdapter* Adapter = MotionWarpingComp ? MotionWarpingComp->GetOwnerAdapter() : nullptr;
	const FMotionWarpingTarget* Target = Warp && MotionWarpingComp ? MotionWarpingComp->FindWarpTarget(Warp->WarpTargetName) : nullptr;
	if (!Warp || !Adapter || !Target)
	{
		return;
	}

	// 비활성화는 이번 이동 전에 온다. EndTime 을 넘친 양까지 보이도록 두 시각을 함께 기록한다.
	const FTransform TargetTransform = Target->GetTargetTrasform();
	const FVector ActualLocation = Adapter->GetVisualRootLocation();
	const float ActualYaw = Adapter->GetCurrentRotation().Rotator().Yaw;
	VB_LOG(Log, "InteractionWarp end: actor=%s montage=%s reachedWindowEnd=%d sample=%.3f end=%.3f distance2D=%.2fcm yawError=%.2fdeg actual=%s target=%s",
		*GetNameSafe(MotionWarpingComp->GetOwner()), *GetPathNameSafe(Warp->GetAnimation()),
		Warp->PreviousPosition >= Warp->EndTime, Warp->PreviousPosition, Warp->EndTime,
		FVector::Dist2D(ActualLocation, TargetTransform.GetLocation()),
		FMath::Abs(FMath::FindDeltaAngleDegrees(ActualYaw, TargetTransform.Rotator().Yaw)),
		*ActualLocation.ToCompactString(), *TargetTransform.GetLocation().ToCompactString());
	if (FVector::Dist2D(ActualLocation, TargetTransform.GetLocation()) > 5.0f)
	{
		const ACharacter* Character = Cast<ACharacter>(MotionWarpingComp->GetOwner());
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		const UWorld* World = Character ? Character->GetWorld() : nullptr;
		if (Capsule && World)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(VBInteractionWarpResidual), false, Character);
			if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
			{
				if (const IVBInteractionAnimInterface* InteractionAnim = Cast<IVBInteractionAnimInterface>(Mesh->GetAnimInstance()))
				{
					Params.AddIgnoredActor(UPoseSearchLibrary::GetActorForRole(InteractionAnim->GetInteractionResult(),
						AlignmentAnchorRole));
				}
			}

			for (const UPrimitiveComponent* Ignored : Capsule->GetMoveIgnoreComponents())
			{
				Params.AddIgnoredComponent(Ignored);
			}

			FHitResult Hit;
			const FVector Delta = TargetTransform.GetLocation() - ActualLocation;
			const bool bBlocked = World->SweepSingleByChannel(Hit, Capsule->GetComponentLocation(),
				Capsule->GetComponentLocation() + Delta, Capsule->GetComponentQuat(),
				Capsule->GetCollisionObjectType(),
				FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
				Params, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
			VB_LOG(Warning, "InteractionWarp residual: actor=%s blocked=%d blocker=%s component=%s hitTime=%.3f penetrating=%d",
				*GetNameSafe(Character), bBlocked, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
				Hit.Time, Hit.bStartPenetrating);
		}
	}
}

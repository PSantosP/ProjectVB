// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBTraversalMontage.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBClimbAnimSet.h"
#include "GameFramework/Character.h"

UAnimInstance* VBTraversalMontage::FindAnim(const ACharacter* Character)
{
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return Mesh ? Mesh->GetAnimInstance() : nullptr;
}

float VBTraversalMontage::StopBlendTime(const UVBClimbAnimSet* Clips)
{
	return (Clips ? Clips : GetDefault<UVBClimbAnimSet>())->MontageStopBlendTime;
}

UAnimMontage* VBTraversalMontage::DriveLoopingMontage(UAnimInstance* Anim, UAnimMontage* Wanted, UAnimMontage* Active,
	float StopBlend, bool& bOutStarted, float StartTime)
{
	bOutStarted = false;
	if (!Anim)
	{
		return nullptr;
	}

	if (!Wanted)
	{
		if (Active)
		{
			Anim->Montage_Stop(StopBlend, Active);
		}

		return nullptr;
	}

	if (Active != Wanted || !Anim->Montage_IsPlaying(Active))
	{
		// 같은 그룹을 새 몽타주의 블렌드인으로 정지시켜 나가는 쪽과 들어오는 쪽의 가중치 합을 1 로 유지한다.
		Anim->Montage_Play(Wanted, 1.0f, EMontagePlayReturnType::MontageLength, StartTime, /*bStopAllMontages=*/true);
		// 섹션 반복으로 돈다. 끝날 때 다시 재생하면 블렌드아웃만큼 일찍 끝나 매 주기 포즈가 튄다.
		if (Wanted->GetNumSections() > 0)
		{
			const FName LoopSection = Wanted->GetSectionName(0);
			Anim->Montage_SetNextSection(LoopSection, LoopSection, Wanted);
		}

		bOutStarted = true;
	}

	return Wanted;
}

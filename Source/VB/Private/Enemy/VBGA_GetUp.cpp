// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBGA_GetUp.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBRagdollGetUpTypes.h"
#include "Animation/VBEnemyAnimInstance.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Chooser.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BoneIndices.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Enemy/VBEnemyRagdollComponent.h"
#include "Data/VBRagdollConfig.h"
#include "VB/VB.h"

namespace
{
	struct FFinisherBodyBasis
	{
		FVector Long;
		FVector Chest;
	};

	bool MakeBodyBasis(const FVector* Positions, FFinisherBodyBasis& OutBasis)
	{
		OutBasis.Long = (Positions[1] - Positions[0]).GetSafeNormal();
		const FVector Lateral = (Positions[3] - Positions[2]).GetSafeNormal();
		OutBasis.Chest = FVector::CrossProduct(Lateral, OutBasis.Long).GetSafeNormal();
		return !OutBasis.Long.IsNearlyZero() && !OutBasis.Chest.IsNearlyZero();
	}

	// 고정 처형 몽타주는 저작된 시작 방향이 있어 쓰러진 캐릭터 방향에 맞춰야 한다(GASP Chooser 경로와 다름).
	bool MeasureFinisherGetUpYaw(const USkeletalMeshComponent& Mesh, const UAnimMontage& Montage,
		float& OutYaw, FString& OutWhy)
	{
		const FAnimSegment* Segment = Montage.SlotAnimTracks.IsEmpty() ? nullptr
			: Montage.SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(0.0f);
		const UAnimSequence* Clip = Segment ? Cast<UAnimSequence>(Segment->GetAnimReference()) : nullptr;
		const USkeleton* Skeleton = Clip ? Clip->GetSkeleton() : nullptr;
		if (!Clip || !Skeleton || Segment->AnimEndTime <= Segment->AnimStartTime)
		{
			OutWhy = TEXT("기상 시작점에 유효한 시퀀스가 없다");
			return false;
		}

		static const FName BoneNames[] = { TEXT("pelvis"), TEXT("head"), TEXT("clavicle_l"), TEXT("clavicle_r") };
		const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
		const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();
		const FAnimExtractContext Context(Segment->ConvertTrackPosToAnimPos(0.0f));
		FVector CurrentPositions[4];
		FVector ClipPositions[4];
		for (int32 Bone = 0; Bone < UE_ARRAY_COUNT(BoneNames); ++Bone)
		{
			int32 Index = RefSkel.FindBoneIndex(BoneNames[Bone]);
			if (Index == INDEX_NONE || Mesh.GetBoneIndex(BoneNames[Bone]) == INDEX_NONE)
			{
				OutWhy = FString::Printf(TEXT("기상 방향 측정 본이 없다: %s"), *BoneNames[Bone].ToString());
				return false;
			}

			CurrentPositions[Bone] = Mesh.GetBoneLocation(BoneNames[Bone], EBoneSpaces::WorldSpace);
			FTransform ComponentSpace = FTransform::Identity;
			while (Index != INDEX_NONE)
			{
				// 애니 트랙이 없는 뼈는 레퍼런스 변환을 유지한다.
				FTransform Local = RefPose[Index];
				Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Index), Context, false);
				ComponentSpace *= Local;
				Index = RefSkel.GetParentIndex(Index);
			}

			ClipPositions[Bone] = ComponentSpace.GetLocation();
		}

		FFinisherBodyBasis CurrentBasis;
		FFinisherBodyBasis ClipBasis;
		if (!MakeBodyBasis(CurrentPositions, CurrentBasis) || !MakeBodyBasis(ClipPositions, ClipBasis))
		{
			OutWhy = TEXT("기상 방향을 정할 수 없는 본 위치");
			return false;
		}

		const FVector ClipLongWorld = Mesh.GetComponentQuat().RotateVector(ClipBasis.Long);
		if (CurrentBasis.Long.SizeSquared2D() <= KINDA_SMALL_NUMBER || ClipLongWorld.SizeSquared2D() <= KINDA_SMALL_NUMBER)
		{
			OutWhy = TEXT("몸의 세로축이 수직이라 수평 기상 방향을 정할 수 없다");
			return false;
		}

		OutYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(
			FMath::Atan2(CurrentBasis.Long.Y, CurrentBasis.Long.X) - FMath::Atan2(ClipLongWorld.Y, ClipLongWorld.X)));
		const FVector ClipChestWorld = Mesh.GetComponentQuat().RotateVector(ClipBasis.Chest);
		if ((CurrentBasis.Chest.Z < 0.0) != (ClipChestWorld.Z < 0.0))
		{
			OutWhy = TEXT("쓰러진 자세와 기상 클립의 엎드림/누움이 다르다 - yaw만으로는 맞출 수 없다");
		}

		return FMath::IsFinite(OutYaw);
	}

	// 고정 몽타주 정렬을 애니메이션 쪽으로 넘길 때 필요한 값. Anim 이 null 이면 넘길 것이 없다.
	struct FFinisherGetUpHandoff
	{
		UVBEnemyAnimInstance* Anim = nullptr;
		FTransform Before = FTransform::Identity;
		FTransform After = FTransform::Identity;
		FVector PelvisBefore = FVector::ZeroVector;
	};

	// 래그돌 설정의 Chooser 로 물리 포즈에 맞는 기상 몽타주와 시작 시각을 고른다.
	void ChooseRagdollGetUp(const UVBRagdollConfig& Config, const AActor* Avatar, UAnimInstance* AnimInstance,
		const UAnimMontage*& OutMontage, double& OutStartTime)
	{
		const ACharacter* Character = Cast<ACharacter>(Avatar);
		const UVBRagdollComponent* Ragdoll = Avatar ? Avatar->FindComponentByClass<UVBRagdollComponent>() : nullptr;
		const FAnimNode_PoseSearchHistoryCollector_Base* HistoryNode = AnimInstance
			? UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), AnimInstance) : nullptr;
		if (!Config.RecoverableGetUpChooser || !Character || !Ragdoll || !HistoryNode || !HistoryNode->GetPoseHistoryPtr())
		{
			VB_LOG(Error, "GASPGetUp: missing native chooser or active PoseHistory on %s", *GetNameSafe(Avatar));
			return;
		}

		FVBRagdollGetUpInputs Inputs;
		Inputs.MovementMode = Character->GetCharacterMovement()->MovementMode;
		Inputs.RollingGetup = Ragdoll->ShouldPlayRollingGetUp();
		Inputs.PoseHistory = HistoryNode->GetPoseHistoryReference();
		FVBRagdollGetUpOutputs Outputs;
		FChooserEvaluationContext Context(AnimInstance);
		Context.AddStructParam(Inputs);
		Context.AddStructParam(Outputs);
		UChooserTable::EvaluateChooser(Context, Config.RecoverableGetUpChooser,
			FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&OutMontage](UObject* Result)
			{
				OutMontage = Cast<UAnimMontage>(Result);
				return OutMontage ? FObjectChooserBase::EIteratorStatus::Stop : FObjectChooserBase::EIteratorStatus::Continue;
			}));
		OutStartTime = Outputs.MontageStartTime;
		VB_LOG(Log, "GASPGetUp query: actor=%s speedXY=%.1f rolling=%d mode=%d montage=%s start=%.3f",
			*GetNameSafe(Avatar), Ragdoll->GetAnimationState().CenterOfMassVelocity.Size2D(),
			Inputs.RollingGetup, int32(Inputs.MovementMode), *GetNameSafe(OutMontage), OutStartTime);
	}

	// 고른 몽타주가 이 몸의 골격에 맞고 시작 시각이 재생 범위 안인가.
	bool IsGetUpPlayable(const UAnimMontage& Montage, const USkeletalMeshComponent* Mesh, double StartTime)
	{
		return Mesh && Mesh->GetSkeletalMeshAsset()
			&& Montage.GetSkeleton() == Mesh->GetSkeletalMeshAsset()->GetSkeleton()
			&& FMath::IsFinite(StartTime) && StartTime >= 0.0 && StartTime < Montage.GetPlayLength();
	}

	// 고정 몽타주를 쓰러진 캐릭터 방향에 맞춘다. 적 AnimInstance 면 넘길 값을 돌려주고, 아니면 액터를 바로 돌린다.
	FFinisherGetUpHandoff AlignFinisherGetUp(AActor& Avatar, const USkeletalMeshComponent& Mesh, const UAnimMontage& Montage,
		UAnimInstance* AnimInstance, const FGameplayEventData* TriggerEventData)
	{
		FFinisherGetUpHandoff Handoff;
		float MeasuredYaw = 0.0f;
		FString Why;
		const bool bMeasured = MeasureFinisherGetUpYaw(Mesh, Montage, MeasuredYaw, Why);
		const float Trim = TriggerEventData && FMath::IsFinite(TriggerEventData->EventMagnitude)
			? TriggerEventData->EventMagnitude : 0.0f;
		const float YawOffset = FRotator::NormalizeAxis((bMeasured ? MeasuredYaw : 0.0f) + Trim);
		Handoff.Anim = Cast<UVBEnemyAnimInstance>(AnimInstance);
		if (Handoff.Anim)
		{
			Handoff.Before = Avatar.GetActorTransform();
			Handoff.After = Handoff.Before;
			Handoff.PelvisBefore = Mesh.GetBoneLocation(TEXT("pelvis"), EBoneSpaces::WorldSpace);
			const FTransform Root = Mesh.GetSocketTransform(TEXT("root"), RTS_World);
			const FVector Offset = Root.GetLocation() - Mesh.GetComponentLocation();
			const float RootYaw = FRotator::NormalizeAxis(Root.Rotator().Yaw - Mesh.GetComponentRotation().Yaw);
			Handoff.After.AddToTranslation(FVector(Offset.X, Offset.Y, 0.0));
			Handoff.After.SetRotation(FRotator(0.0f, RootYaw + YawOffset, 0.0f).Quaternion() * Handoff.Before.GetRotation());
		}
		else if (!FMath::IsNearlyZero(YawOffset))
		{
			Avatar.AddActorWorldRotation(FRotator(0.0f, YawOffset, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			Avatar.ForceNetUpdate();
		}

		VB_LOG(Log, "FinisherGetUp alignment: actor=%s montage=%s measured=%d measuredYaw=%.2f trim=%.2f targetYawDelta=%.2f deferred=%d",
			*Avatar.GetName(), *Montage.GetName(), bMeasured ? 1 : 0, MeasuredYaw, Trim, YawOffset, Handoff.Anim ? 1 : 0);
		if (!Why.IsEmpty())
		{
			VB_LOG(Warning, "FinisherGetUp alignment: %s - %s", *Avatar.GetName(), *Why);
		}

		return Handoff;
	}
}

UVBGA_GetUp::UVBGA_GetUp()
{
	// 트리거 등록은 ASC 가 OnGiveAbility 때 AbilityTriggers 로 한다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_FinisherSurvived;
	AbilityTriggers.Add(Trigger);

	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Recovery_GetUp));

	// 일어나는 동안은 「일어나는 중」이다. 이 태그의 단독 생산자다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_GettingUp);

	// 일어나는 동안의 막기는 전투 규칙 표(DA_CombatRules)에 있다.
	// FinisherWindow 로는 막지 않는다 - 버티고 일어난 적도 그 태그를 들고 있다.

	// 활성 중 재활성화 허용 - false 면 연속 처형의 두 번째 기상이 조용히 거부된다.
	bRetriggerInstancedAbility = true;
}

void UVBGA_GetUp::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	AActor* const Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	USkeletalMeshComponent* const Mesh = ActorInfo ? ActorInfo->SkeletalMeshComponent.Get() : nullptr;
	UAnimInstance* const AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	const UObject* Selection = TriggerEventData ? TriggerEventData->OptionalObject.Get() : nullptr;
	const UAnimMontage* Montage = Cast<UAnimMontage>(Selection);
	const bool bFixedFinisherMontage = Montage != nullptr;
	double StartTime = 0.0;
	if (const UVBRagdollConfig* Config = Cast<UVBRagdollConfig>(Selection))
	{
		ChooseRagdollGetUp(*Config, Avatar, AnimInstance, Montage, StartTime);
	}

	if (Montage && !IsGetUpPlayable(*Montage, Mesh, StartTime))
	{
		VB_LOG(Error, "GASPGetUp: invalid montage skeleton/start time (%s, %.3f)", *GetNameSafe(Montage), StartTime);
		Montage = nullptr;
	}

	if (!Montage)
	{
		EndWithoutMontage(Handle, ActorInfo, ActivationInfo);
		return;
	}

	FFinisherGetUpHandoff Handoff;
	// 비물리 고정 몽타주 경로만 정렬한다. GASP 포즈 매칭 기상에 걸면 두 번 돈다.
	if (bFixedFinisherMontage && Avatar && Avatar->HasAuthority() && !Mesh->IsAnySimulatingPhysics())
	{
		Handoff = AlignFinisherGetUp(*Avatar, *Mesh, *Montage, AnimInstance, TriggerEventData);
	}

	PlayGetUpMontage(*Montage, StartTime);
	// ASC 가 외부 몽타주를 점유하고 있으면 PlayMontageAndWait 가 동기로 취소해 OnGetUpEnded 가 이미 끝냈을 수 있다.
	if (!IsActive())
	{
		return;
	}

	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(Montage))
	{
		VB_LOG(Error, "%s: 기상 몽타주 재생 상태를 확인하지 못했다 (%s)", *GetName(), *Montage->GetName());
		OnGetUpEnded();
		return;
	}

	if (Handoff.Anim)
	{
		Handoff.Anim->QueueFinisherGetUpHandoff(const_cast<UAnimMontage*>(Montage), Handoff.Before, Handoff.After, Handoff.PelvisBefore);
	}

	VB_LOG(Log, "GASPGetUp playing: actor=%s montage=%s start=%.3f yaw=%.1f physics=%d source=%s",
		*GetNameSafe(Avatar), *Montage->GetName(), StartTime,
		Avatar ? Avatar->GetActorRotation().Yaw : 0.0f, Mesh && Mesh->IsAnySimulatingPhysics(),
		bFixedFinisherMontage ? TEXT("FinisherMontage") : TEXT("GASPChooser"));
}

void UVBGA_GetUp::EndWithoutMontage(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (UVBEnemyRagdollComponent* Ragdoll = UVBEnemyRagdollComponent::FindFor(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		Ragdoll->FinishGetUp();
	}
	// 비어 있는 것은 결함이 아니지만, 원인을 가리도록 한 줄 남긴다.
	VB_LOG(Warning, "%s: 유효한 기상 검색 결과가 없다 (Chooser, PoseHistory, 골격 및 재생 범위를 확인)", *GetName());
	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_GetUp::PlayGetUpMontage(const UAnimMontage& Montage, double StartTime)
{
	// bStopAllMontages=true - 블렌드아웃 중인 처형 몽타주와 자세가 섞이지 않게.
	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, const_cast<UAnimMontage*>(&Montage),
			/*Rate=*/1.0f, NAME_None, /*bStopWhenAbilityEnds=*/true,
			/*AnimRootMotionTranslationScale=*/1.0f, /*StartTimeSeconds=*/static_cast<float>(StartTime),
			/*bAllowInterruptAfterBlendOut=*/false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnGetUpEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnGetUpEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnGetUpEnded);
	MontageTask->ReadyForActivation();
}

void UVBGA_GetUp::OnGetUpEnded()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_GetUp::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	if (UVBEnemyAnimInstance* EnemyAnim = ActorInfo ? Cast<UVBEnemyAnimInstance>(ActorInfo->GetAnimInstance()) : nullptr)
	{
		EnemyAnim->CancelFinisherGetUpHandoff();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (UVBEnemyRagdollComponent* Ragdoll = UVBEnemyRagdollComponent::FindFor(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		Ragdoll->FinishGetUp();
	}
}

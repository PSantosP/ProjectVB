#include "Character/VBFinisherSpaceRules.h"

#include "Animation/AnimMontage.h"
#include "Animation/VBAnimNotifyState_InteractionWarp.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "RootMotionModifier.h"
#include "VB/VB.h"

namespace
{
	// 재생할 역할의 정렬된 루트 경로를 샘플한다.
	bool MakeSpacePath(const FPoseSearchBlueprintResult& Result, const FVBFinisherSpaceSettings& Settings,
		FVBFinisherSpacePath& OutPath)
	{
		const ACharacter* Character = Cast<ACharacter>(UPoseSearchLibrary::GetActor(Result));
		const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(Result.SelectedAnim);
		const UAnimMontage* Montage = Asset ? Cast<UAnimMontage>(Asset->GetAnimationAsset(Result.Role)) : nullptr;
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		if (!Settings.IsValid() || !Montage || !Capsule || !Character->GetMesh()
			|| !Character->GetCharacterMovement() || !FMath::IsFinite(Result.SelectedTime)
			|| Result.SelectedTime < 0.0f)
		{
			return false;
		}

		const UVBAnimNotifyState_InteractionWarp* Warp = nullptr;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			const auto* Candidate = Cast<UVBAnimNotifyState_InteractionWarp>(Event.NotifyStateClass);
			if (!Candidate)
			{
				continue;
			}
			if (Warp && Warp->AlignmentAnchorRole != Candidate->AlignmentAnchorRole)
			{
				return false;
			}
			const auto* Modifier = Cast<URootMotionModifier_Warp>(Candidate->RootMotionModifier);
			if (!Modifier || Modifier->WarpTargetName.IsNone())
			{
				return false;
			}
			Warp = Candidate;
		}
		if (!Warp)
		{
			return false;
		}
		const float Duration = Montage->GetPlayLength() - Result.SelectedTime;
		if (!FMath::IsFinite(Duration) || Duration <= 0.0f
			|| Duration / Settings.SampleInterval > Settings.MaxSamplesPerRole - 1)
		{
			return false;
		}
		const int32 Intervals = FMath::Max(1, FMath::CeilToInt(Duration / Settings.SampleInterval));
		TArray<FTransform> Roots;
		Roots.SetNum(Intervals + 1);
		for (int32 Index = 0; Index <= Intervals; ++Index)
		{
			if (!UVBAnimNotifyState_InteractionWarp::CalculateAlignedTransform(Result,
				Duration * Index / Intervals, Warp->AlignmentAnchorRole, Roots[Index]))
			{
				return false;
			}
		}
		if (!VBFinisherSpaceRules::BuildPath(Roots, Character->GetMesh()->GetRelativeTransform(),
			Capsule->GetComponentLocation(), Character->GetCharacterMovement()->IsMovingOnGround(),
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Settings, OutPath))
		{
			return false;
		}
		OutPath.Channel = Capsule->GetCollisionObjectType();
		OutPath.Responses = Capsule->GetCollisionResponseToChannels();
		return true;
	}
}

// 역할 검증을 마친 후보가 두 캡슐의 이동 공간을 갖는지 묻는다.
bool VBFinisherSpaceRules::HasInteractionSpace(TConstArrayView<FPoseSearchBlueprintResult> Results,
	const UVBFinisherConfig* Config)
{
	if (!Config)
	{
		VB_LOG(Warning, "InteractionFinisher: candidate has no config for space validation");
		return false;
	}
	TArray<FVBFinisherSpacePath> Paths;
	TArray<const AActor*> Participants;
	for (const FPoseSearchBlueprintResult& Result : Results)
	{
		if (!MakeSpacePath(Result, Config->Space, Paths.AddDefaulted_GetRef()))
		{
			VB_LOG(Warning, "InteractionFinisher: invalid space path (config=%s role=%s)",
				*GetNameSafe(Config), *Result.Role.ToString());
			return false;
		}
		Participants.Add(UPoseSearchLibrary::GetActor(Result));
	}
	UWorld* World = Participants.IsEmpty() || !Participants[0] ? nullptr : Participants[0]->GetWorld();
	return World && VBFinisherSpaceRules::IsClear(*World, Paths, Participants);
}

bool VBFinisherSpaceRules::BuildPath(TConstArrayView<FTransform> Roots, const FTransform& MeshRelative,
	const FVector& CurrentCenter, bool bGrounded, float Radius, float HalfHeight,
	const FVBFinisherSpaceSettings& Settings, FVBFinisherSpacePath& OutPath)
{
	OutPath = FVBFinisherSpacePath();
	if (!Settings.IsValid() || Roots.Num() < 2 || Roots.Num() > Settings.MaxSamplesPerRole
		|| CurrentCenter.ContainsNaN() || MeshRelative.ContainsNaN()
		|| MeshRelative.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Radius) || Radius <= 0.0f || !FMath::IsFinite(HalfHeight)
		|| HalfHeight - Settings.GroundTolerance < Radius + Settings.HorizontalMargin)
	{
		return false;
	}

	FVBFinisherSpacePath Path;
	Path.Radius = Radius + Settings.HorizontalMargin;
	Path.HalfHeight = HalfHeight - Settings.GroundTolerance;
	Path.Centers.Reserve(Roots.Num() + 1);
	Path.Centers.Add(CurrentCenter);
	const FTransform MeshInverse = MeshRelative.Inverse();
	for (const FTransform& Root : Roots)
	{
		if (Root.ContainsNaN())
		{
			return false;
		}
		FVector Center = (MeshInverse * Root).GetLocation();
		if (bGrounded)
		{
			Center.Z = CurrentCenter.Z;
		}
		if (Center.ContainsNaN())
		{
			return false;
		}
		Path.Centers.Add(Center);
	}
	OutPath = MoveTemp(Path);
	return true;
}

bool VBFinisherSpaceRules::IsClear(UWorld& World, TConstArrayView<FVBFinisherSpacePath> Paths,
	TConstArrayView<const AActor*> Participants)
{
	if (Paths.IsEmpty() || Paths.Num() != Participants.Num())
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBFinisherSpace), false);
	for (const AActor* Participant : Participants)
	{
		if (!IsValid(Participant) || Participant->GetWorld() != &World)
		{
			return false;
		}
		Query.AddIgnoredActor(Participant);
		TArray<AActor*> Attached;
		Participant->GetAttachedActors(Attached, true, true);
		for (AActor* Attachment : Attached)
		{
			if (IsValid(Attachment) && Attachment->IsOwnedBy(Participant))
			{
				Query.AddIgnoredActor(Attachment);
			}
		}
	}

	for (const FVBFinisherSpacePath& Path : Paths)
	{
		if (Path.Centers.Num() < 2 || !FMath::IsFinite(Path.Radius) || Path.Radius <= 0.0f
			|| !FMath::IsFinite(Path.HalfHeight) || Path.HalfHeight < Path.Radius
			|| Path.Channel >= ECC_MAX)
		{
			return false;
		}
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Path.Radius, Path.HalfHeight);
		const FCollisionResponseParams Responses(Path.Responses);
		for (int32 Index = 0; Index < Path.Centers.Num(); ++Index)
		{
			const FVector& Center = Path.Centers[Index];
			if (Center.ContainsNaN() || World.OverlapBlockingTestByChannel(Center, FQuat::Identity,
				Path.Channel, Shape, Query, Responses))
			{
				return false;
			}
			if (Index > 0 && World.SweepTestByChannel(Path.Centers[Index - 1], Center, FQuat::Identity,
				Path.Channel, Shape, Query, Responses))
			{
				return false;
			}
		}
	}
	return true;
}

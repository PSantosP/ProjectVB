// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Animation/VBGASPAnimNotifies.h"

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNode_VBMuteEffects.h" // 숨은 갈래 노티파이 거르기
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Animation/BlendProfile.h"
#include "Animation/VBGASPNotifyInterface.h"
#include "Animation/VBLimbBones.h"
#include "Camera/CameraShakeBase.h" // SimpleCameraShake 의 TSubclassOf 판정 - 공용 PCH 에 기대지 않는다(PCH 없는 -SingleFile 확인에서 C2027)
#include "Character/VBContextEffectComponent.h" // Foley 노티파이가 새 이펙트 장치를 먼저 부른다
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "VB/VB.h" // VB_LOG

namespace
{
	bool IsGameMesh(const USkeletalMeshComponent* Mesh)
	{
		return IsValid(Mesh) && IsValid(Mesh->GetOwner()) && Mesh->GetWorld() && Mesh->GetWorld()->IsGameWorld();
	}

	UAnimInstance* FindTransitionReceiver(USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return nullptr;
		}
		// state machine 은 linked locomotion instance 가 가지므로 host graph 보다 먼저 본다
		for (UAnimInstance* Linked : static_cast<const USkeletalMeshComponent*>(Mesh)->GetLinkedAnimInstances())
		{
			if (IsValid(Linked) && Linked->GetClass()->ImplementsInterface(UVBGASPNotifyInterface::StaticClass()))
			{
				return Linked;
			}
		}

		UAnimInstance* Anim = Mesh->GetAnimInstance();
		return Anim && Anim->GetClass()->ImplementsInterface(UVBGASPNotifyInterface::StaticClass()) ? Anim : nullptr;
	}
}

void UVBGASPNotifyState_EarlyTransition::NotifyTick(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	if (UAnimNotifyLibrary::IsBlendingOut(EventReference))
	{
		return;
	}

	if (UAnimInstance* Receiver = FindTransitionReceiver(MeshComp))
	{
		if (TransitionCondition == EVBGASPEarlyTransitionCondition::Always ||
			IVBGASPNotifyInterface::Execute_GetGASPNotifyGait(Receiver) != GaitNotEqual)
		{
			IVBGASPNotifyInterface::Execute_RequestGASPEarlyTransition(Receiver,
				TransitionDestination == EVBGASPEarlyTransitionDestination::ToLoop);
		}
	}
}

void UVBGASPNotifyState_MontageBlendOut::NotifyTick(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	ACharacter* Character = MeshComp ? Cast<ACharacter>(MeshComp->GetOwner()) : nullptr;
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	UAnimInstance* Anim = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
	if (!Movement || !Anim || !Montage)
	{
		return;
	}

	const bool bBlendOut = BlendOutCondition == EVBGASPMontageBlendOutCondition::ForceBlendOut ||
		(BlendOutCondition == EVBGASPMontageBlendOutCondition::WithMovementInput
			&& !Movement->GetCurrentAcceleration().IsNearlyZero(0.1f)) ||
		(BlendOutCondition == EVBGASPMontageBlendOutCondition::IfFalling && Movement->IsFalling());
	if (!bBlendOut)
	{
		return;
	}

	const FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
		EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	// 예전 재생에서 밀려온 notify 가 같은 Montage 의 새 재생을 멈추면 안 된다
	if (Instance && (!Context || Context->MontageInstanceID == Instance->GetInstanceID()))
	{
		FMontageBlendSettings Settings;
		Settings.Blend.BlendTime = FMath::Max(0.f, BlendOutTime);
		Settings.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
		Settings.BlendMode = EMontageBlendMode::Standard;
		// 엔진 조회는 const 로 주지만 FMontageBlendSettings 는 asset 을 바꾸지 않는 참조로만 담는다
		Settings.BlendProfile = const_cast<UBlendProfile*>(Anim->GetBlendProfileByName(BlendProfile));
		Anim->Montage_StopWithBlendSettings(Settings, Montage);
	}
}

void UVBGASPNotifyState_MontageBlendOut::BranchingPointNotifyTick(FBranchingPointNotifyPayload& Payload, float FrameDeltaTime)
{
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	NotifyTick(Payload.SkelMeshComponent, Payload.SequenceAsset, FrameDeltaTime, Reference);
}

UVBGASPNotifyState_OverrideCapsuleCollision::UVBGASPNotifyState_OverrideCapsuleCollision()
{
	NotifyStateBehaviorFlags |= static_cast<uint8>(EAnimNotifyStateBehaviorFlags::NoMergeOnConcurrentPlay);
}

void UVBGASPNotifyState_OverrideCapsuleCollision::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(MeshComp, true))
	{
		State->BeginCollision(MeshComp, this, EventReference.GetNotifyInstanceID());
	}
}

void UVBGASPNotifyState_OverrideCapsuleCollision::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(MeshComp, false))
	{
		State->EndCollision(MeshComp, this, EventReference.GetNotifyInstanceID());
	}
}

void UVBGASPNotifyState_OverrideCapsuleCollision::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(Payload.SkelMeshComponent, true))
	{
		State->BeginCollision(Payload.SkelMeshComponent, this, Payload.MontageInstanceID);
	}
}

void UVBGASPNotifyState_OverrideCapsuleCollision::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(Payload.SkelMeshComponent, false))
	{
		State->EndCollision(Payload.SkelMeshComponent, this, Payload.MontageInstanceID);
	}
}

UVBGASPNotifyState_OverrideMovementMode::UVBGASPNotifyState_OverrideMovementMode()
{
	NotifyStateBehaviorFlags |= static_cast<uint8>(EAnimNotifyStateBehaviorFlags::NoMergeOnConcurrentPlay);
}

void UVBGASPNotifyState_OverrideMovementMode::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	if (!bApplyToCMC)
	{
		return;
	}

	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(MeshComp, true))
	{
		State->BeginMovement(MeshComp, this, EventReference.GetNotifyInstanceID());
	}
}

void UVBGASPNotifyState_OverrideMovementMode::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!bApplyToCMC)
	{
		return;
	}

	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(MeshComp, false))
	{
		State->EndMovement(MeshComp, this, EventReference.GetNotifyInstanceID());
	}
}

void UVBGASPNotifyState_OverrideMovementMode::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
	if (!bApplyToCMC)
	{
		return;
	}

	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(Payload.SkelMeshComponent, true))
	{
		State->BeginMovement(Payload.SkelMeshComponent, this, Payload.MontageInstanceID);
	}
}

void UVBGASPNotifyState_OverrideMovementMode::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
	if (!bApplyToCMC)
	{
		return;
	}

	if (UVBGASPNotifyStateComponent* State = UVBGASPNotifyStateComponent::Find(Payload.SkelMeshComponent, false))
	{
		State->EndMovement(Payload.SkelMeshComponent, this, Payload.MontageInstanceID);
	}
}

void UVBGASPAnimNotify_SimpleCameraShake::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (IsGameMesh(MeshComp) && Shake && MeshComp->GetWorld()->GetNetMode() != NM_DedicatedServer
		&& !VBAnimEffects::IsMuted(EventReference))
	{
		const float Inner = FMath::Max(0.f, InnerRadius);
		UGameplayStatics::PlayWorldCameraShake(MeshComp, Shake, MeshComp->GetOwner()->GetActorLocation(),
			Inner, FMath::Max(Inner, OuterRadius), FMath::Max(0.f, Falloff), false);
	}
}

void UVBGASPAnimNotify_FoleyEvent::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	// 숨은 갈래(슬라이드 중 갱신만 하는 본 이동 그래프)의 발소리는 안 튼다 - 보이는 슬라이드 그래프와 두 번 울린다(설계 slide-sprint-resume 5차).
	if (!IsValid(MeshComp) || !MeshComp->GetOwner() || !MeshComp->GetWorld() || !Event.IsValid() ||
		MeshComp->GetWorld()->GetNetMode() == NM_DedicatedServer || UAnimNotifyLibrary::IsBlendingOut(EventReference)
		|| VBAnimEffects::IsMuted(EventReference))
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	// 발소리는 UVBContextEffectComponent 하나로 낸다 - Foley 태그가 곧 이펙트 태그이고 Side 가 뼈를 정한다
	// 이 notify 클래스는 자산 1,764개가 들고 있어 이름과 클래스를 바꾸지 않는다
	if (UVBContextEffectComponent* Effects = Owner->FindComponentByClass<UVBContextEffectComponent>())
	{
		const FName Bone = Side == EVBGASPFoleySide::Left ? VBLimbBones::FootL
			: (Side == EVBGASPFoleySide::Right ? VBLimbBones::FootR : FName(NAME_None));
		Effects->PlayEffect(Event, MeshComp, Bone, VolumeMultiplier, PitchMultiplier);
		return;
	}
	// 컴포넌트가 없는 액터. 애님 에디터 미리보기면 뱅크 소리를 2D 로 들려준다(저작 중 듣는 용도 - 뱅크가 남은 이유).
	// 게임 액터면 설정 누락이므로 소리를 대신 내지 않고 경고한다
	if (MeshComp->GetWorld()->WorldType == EWorldType::EditorPreview)
	{
		if (const TObjectPtr<USoundBase>* Sound = Bank ? Bank->Assets.Find(Event) : nullptr; Sound && IsValid(Sound->Get()))
		{
			UGameplayStatics::PlaySound2D(MeshComp, Sound->Get(), FMath::Max(0.f, VolumeMultiplier), FMath::Max(0.01f, PitchMultiplier));
		}

		return;
	}
#if !UE_BUILD_SHIPPING
	static TSet<FName> WarnedClasses;
	if (!WarnedClasses.Contains(Owner->GetClass()->GetFName()))
	{
		WarnedClasses.Add(Owner->GetClass()->GetFName());
		VB_LOG(Warning, "[이펙트] %s 에 UVBContextEffectComponent 가 없어 Foley 노티파이(%s)가 소리를 못 낸다 - 클래스당 한 번",
			*Owner->GetClass()->GetName(), *Event.ToString());
	}
#endif
}

bool FVBGASPNotifyWindow::Matches(USkeletalMeshComponent* InMesh, UAnimNotifyState* InNotify, int32 InInstanceId) const
{
	return Mesh.Get() == InMesh && Notify.Get() == InNotify && InstanceId == InInstanceId;
}

UVBGASPNotifyStateComponent::UVBGASPNotifyStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UVBGASPNotifyStateComponent* UVBGASPNotifyStateComponent::Find(USkeletalMeshComponent* MeshComp, bool bCreate)
{
	if (!IsGameMesh(MeshComp))
	{
		return nullptr;
	}

	AActor* Owner = MeshComp->GetOwner();
	// collision, movement 변경은 authority 가 하고, 예측하는 autonomous CMC 도 허용한다
	if (!Owner->HasAuthority() && Owner->GetLocalRole() != ROLE_AutonomousProxy)
	{
		return nullptr;
	}

	UVBGASPNotifyStateComponent* State = Owner->FindComponentByClass<UVBGASPNotifyStateComponent>();
	if (!State && bCreate)
	{
		State = NewObject<UVBGASPNotifyStateComponent>(Owner);
		State->RegisterComponent();
	}

	return State;
}

bool UVBGASPNotifyStateComponent::StillOwnsCollision() const
{
	const UPrimitiveComponent* Target = CollisionTarget.Get();
	return Target && Target->GetCollisionResponseToChannels() == AppliedResponses &&
		Target->GetCollisionProfileName() == AppliedProfile && Target->GetCollisionEnabled() == OriginalCollisionEnabled;
}

void UVBGASPNotifyStateComponent::BeginCollision(USkeletalMeshComponent* MeshComp,
	UVBGASPNotifyState_OverrideCapsuleCollision* Notify, int32 InstanceId)
{
	UPrimitiveComponent* Target = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!Target || !Notify || MeshComp->IsAnySimulatingPhysics())
	{
		return;
	}

	if (!CollisionWindows.IsEmpty() && (CollisionTarget != Target || !StillOwnsCollision()))
	{
		CollisionWindows.Reset();
	}

	if (CollisionWindows.ContainsByPredicate(
		[&](const FVBGASPNotifyWindow& Window) { return Window.Matches(MeshComp, Notify, InstanceId); }))
	{
		return;
	}

	if (CollisionWindows.IsEmpty())
	{
		CollisionTarget = Target;
		OriginalResponses = Target->GetCollisionResponseToChannels();
		OriginalProfile = Target->GetCollisionProfileName();
		OriginalCollisionEnabled = Target->GetCollisionEnabled();
	}

	if (Notify->bResetOnEnd)
	{
		FVBGASPNotifyWindow& Window = CollisionWindows.AddDefaulted_GetRef();
		Window.Mesh = MeshComp;
		Window.Notify = Notify;
		Window.InstanceId = InstanceId;
		Window.Value = Notify->NewResponse;
	}
	else
	{
		// 지속 override 는 앞선 구간을 모두 대신하므로, 그 구간들이 나중에 끝나도 이 요청을 되돌리지 못한다
		CollisionWindows.Reset();
	}

	Target->SetCollisionResponseToAllChannels(Notify->NewResponse);
	AppliedResponses = Target->GetCollisionResponseToChannels();
	// 일부 엔진 setter 는 이름 붙은 profile 을 Custom 으로 바꾸므로 쓴 뒤의 이름을 기록한다
	AppliedProfile = Target->GetCollisionProfileName();
}

void UVBGASPNotifyStateComponent::EndCollision(USkeletalMeshComponent* MeshComp, UAnimNotifyState* Notify, int32 InstanceId)
{
	const int32 Index = CollisionWindows.IndexOfByPredicate(
		[&](const FVBGASPNotifyWindow& Window) { return Window.Matches(MeshComp, Notify, InstanceId); });
	if (Index == INDEX_NONE)
	{
		return;
	}

	if (!StillOwnsCollision() || MeshComp->IsAnySimulatingPhysics())
	{
		CollisionWindows.Reset();
		return;
	}

	const bool bWasTop = Index == CollisionWindows.Num() - 1;
	CollisionWindows.RemoveAt(Index);
	if (bWasTop)
	{
		UPrimitiveComponent* Target = CollisionTarget.Get();
		if (CollisionWindows.IsEmpty())
		{
			if (OriginalProfile != UCollisionProfile::CustomCollisionProfileName && !OriginalProfile.IsNone())
			{
				Target->SetCollisionProfileName(OriginalProfile);
			}

			Target->SetCollisionResponseToChannels(OriginalResponses);
		}
		else
		{
			Target->SetCollisionResponseToAllChannels(static_cast<ECollisionResponse>(CollisionWindows.Last().Value));
		}

		AppliedResponses = Target->GetCollisionResponseToChannels();
		AppliedProfile = Target->GetCollisionProfileName();
	}
}

bool UVBGASPNotifyStateComponent::StillOwnsMovement() const
{
	const UCharacterMovementComponent* Target = MovementTarget.Get();
	return Target && Target->MovementMode == AppliedMode && Target->CustomMovementMode == AppliedCustomMode;
}

void UVBGASPNotifyStateComponent::BeginMovement(USkeletalMeshComponent* MeshComp,
	UVBGASPNotifyState_OverrideMovementMode* Notify, int32 InstanceId)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Target = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Target || !Notify || MeshComp->IsAnySimulatingPhysics() || Target->MovementMode == MOVE_None)
	{
		return;
	}

	if (!MovementWindows.IsEmpty() && (MovementTarget != Target || !StillOwnsMovement()))
	{
		MovementWindows.Reset();
	}

	if (MovementWindows.ContainsByPredicate(
		[&](const FVBGASPNotifyWindow& Window) { return Window.Matches(MeshComp, Notify, InstanceId); }))
	{
		return;
	}

	if (MovementWindows.IsEmpty())
	{
		MovementTarget = Target;
		OriginalMode = Target->MovementMode;
		OriginalCustomMode = Target->CustomMovementMode;
	}

	FVBGASPNotifyWindow& Window = MovementWindows.AddDefaulted_GetRef();
	Window.Mesh = MeshComp;
	Window.Notify = Notify;
	Window.InstanceId = InstanceId;
	Window.Value = Notify->OverrideMovementMode;
	Window.CustomMode = Notify->OverrideCustomMode;
	Window.bRestorePrevious = Notify->bRestorePreviousMode;
	Window.EndingMode = Notify->EndingMovementMode;
	Window.EndingCustomMode = Notify->EndingCustomMode;
	Target->SetMovementMode(Notify->OverrideMovementMode, Notify->OverrideCustomMode);
	AppliedMode = Target->MovementMode;
	AppliedCustomMode = Target->CustomMovementMode;
}

void UVBGASPNotifyStateComponent::EndMovement(USkeletalMeshComponent* MeshComp, UAnimNotifyState* Notify, int32 InstanceId)
{
	const int32 Index = MovementWindows.IndexOfByPredicate(
		[&](const FVBGASPNotifyWindow& Window) { return Window.Matches(MeshComp, Notify, InstanceId); });
	if (Index == INDEX_NONE)
	{
		return;
	}

	if (!StillOwnsMovement() || MeshComp->IsAnySimulatingPhysics())
	{
		MovementWindows.Reset();
		return;
	}

	const FVBGASPNotifyWindow Ending = MovementWindows[Index];
	const bool bWasTop = Index == MovementWindows.Num() - 1;
	MovementWindows.RemoveAt(Index);
	if (!Ending.bRestorePrevious)
	{
		OriginalMode = static_cast<EMovementMode>(Ending.EndingMode);
		OriginalCustomMode = Ending.EndingCustomMode;
	}

	if (bWasTop)
	{
		UCharacterMovementComponent* Target = MovementTarget.Get();
		const EMovementMode Mode = MovementWindows.IsEmpty() ? OriginalMode : static_cast<EMovementMode>(MovementWindows.Last().Value);
		const uint8 CustomMode = MovementWindows.IsEmpty() ? OriginalCustomMode : MovementWindows.Last().CustomMode;
		Target->SetMovementMode(Mode, CustomMode);
		AppliedMode = Target->MovementMode;
		AppliedCustomMode = Target->CustomMovementMode;
	}
}

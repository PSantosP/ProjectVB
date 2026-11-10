// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBLocomotionIntentComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBPawnExtensionComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "CoreLoop/VBServerWrite.h"

UVBLocomotionIntentComponent::UVBLocomotionIntentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // 의도 복제와 질주, 걷기 RPC 의 자리
}

UVBLocomotionIntentComponent* UVBLocomotionIntentComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBLocomotionIntentComponent>() : nullptr;
}

bool UVBLocomotionIntentComponent::IsSprintingFor(const AActor* Actor)
{
	const UVBLocomotionIntentComponent* Found = FindFor(Actor);
	return Found && Found->GetIsSprinting();
}

bool UVBLocomotionIntentComponent::IsAimingFor(const AActor* Actor)
{
	const UVBLocomotionIntentComponent* Found = FindFor(Actor);
	return Found && Found->WantsToAim();
}

void UVBLocomotionIntentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBLocomotionIntentComponent, Intent);
	DOREPLIFETIME(UVBLocomotionIntentComponent, bWantsToAim);
	DOREPLIFETIME(UVBLocomotionIntentComponent, bWantsToStrafe);
}

void UVBLocomotionIntentComponent::StartSprint()
{
	ServerSetSprinting(true);
}

void UVBLocomotionIntentComponent::StopSprint()
{
	ServerSetSprinting(false);
}

void UVBLocomotionIntentComponent::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	SetSprinting(bNewSprinting);
}

void UVBLocomotionIntentComponent::SetSprinting(bool bNewSprinting)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	Intent.bSprinting = bNewSprinting;
	if (bNewSprinting)
	{
		WalkReasons.ClearToggleForSprint();
		RefreshGait();
	}
}

bool UVBLocomotionIntentComponent::CanAim() const
{
	if (const UVBPlayerRagdollComponent* Ragdoll = UVBPlayerRagdollComponent::FindFor(GetOwner()); Ragdoll && Ragdoll->IsRagdollActive())
	{
		return false;
	}

	const UVBPawnExtensionComponent* PawnExtension = UVBPawnExtensionComponent::FindFor(GetOwner());
	const UVBAbilitySystemComponent* ASC = PawnExtension ? PawnExtension->GetAbilitySystemComponent() : nullptr;
	return !(ASC && (UVBAbilitySystemStatics::IsDead(ASC) ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing)));
}

void UVBLocomotionIntentComponent::SetWantsToAim(bool bAim)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || (!Pawn->HasAuthority() && !Pawn->IsLocallyControlled()))
	{
		return;
	}

	bAim = bAim && CanAim();
	bWantsToAim = bAim;
	// 질주는 서버가 정한다 - 소유 클라는 조준만 보내고, 서버가 같은 함수에서 질주를 멈춘다.
	if (bAim && Pawn->HasAuthority())
	{
		SetSprinting(false);
	}

	if (!Pawn->HasAuthority())
	{
		ServerSetWantsToAim(bAim);
	}
}

void UVBLocomotionIntentComponent::ServerSetWantsToAim_Implementation(bool bAim)
{
	SetWantsToAim(bAim);
}

void UVBLocomotionIntentComponent::SetWantsToStrafe(bool bStrafe)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	bWantsToStrafe = bStrafe;
}

void UVBLocomotionIntentComponent::EnterSmartObjectInputState(bool bWalk)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	SetSprinting(false);
	bWantsToAim = false;
	bWantsToStrafe = false;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->UnCrouch();
	}

	WalkReasons.SetSmartObjectOverride(bWalk ? EVBGait::Walk : EVBGait::Run);
	RefreshGait();
	GetOwner()->ForceNetUpdate();
}

void UVBLocomotionIntentComponent::RestoreSmartObjectInputState(bool bPreviousStrafe, bool bPreviousAim)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	WalkReasons.ClearSmartObjectOverride();
	RefreshGait();
	bWantsToStrafe = bPreviousStrafe;
	// 조준은 막는 사실을 다시 본다 - 상호작용 중에 죽거나 래그돌이 됐으면 켜지 않는다.
	bWantsToAim = bPreviousAim && CanAim();
	if (bWantsToAim)
	{
		SetSprinting(false);
	}

	GetOwner()->ForceNetUpdate();
}

void UVBLocomotionIntentComponent::ToggleWalk()
{
	ServerToggleWalk();
}

void UVBLocomotionIntentComponent::ReportMoveInputMagnitude(float StickMagnitude, float WalkRunThreshold)
{
	const bool bAnalogWalk = FVBWalkReasons::IsAnalogWalk(StickMagnitude, WalkRunThreshold);
	if (bAnalogWalk == bAnalogWalkReported)
	{
		return;
	}

	bAnalogWalkReported = bAnalogWalk;
	ServerSetAnalogWalk(bAnalogWalk);
}

void UVBLocomotionIntentComponent::ResetAnalogWalk()
{
	bAnalogWalkReported = false;
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	WalkReasons.SetAnalogWalk(false);
	RefreshGait();
}

void UVBLocomotionIntentComponent::RefreshGait()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	Intent.Gait = WalkReasons.Resolve();
}

void UVBLocomotionIntentComponent::ServerToggleWalk_Implementation()
{
	WalkReasons.ToggleWalk();
	RefreshGait();
}

void UVBLocomotionIntentComponent::ServerSetAnalogWalk_Implementation(bool bAnalogWalk)
{
	WalkReasons.SetAnalogWalk(bAnalogWalk);
	RefreshGait();
}

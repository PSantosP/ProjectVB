// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimGuardHandoff.h"

#include "VBGASPNodeLookup.h"
#include "VBGASPStateView.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Chooser.h"
#include "Data/VBWeaponDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	// 가드 하체 선택의 혼합이 실제로 끝났는가. 선택은 혼합 완료보다 먼저 게시되므로 따로 확인한다.
	bool IsGuardBaseBlendComplete(UAnimInstance* Linked, const UAnimationAsset* Selected)
	{
		if (!Selected)
		{
			return false;
		}

		const FAnimNode_BlendStack_Standalone* Stack =
			VBGASP::FindTaggedNode(Linked, VBGASP::BlendStackTag()).GetAnimNodePtr<FAnimNode_BlendStack_Standalone>();
		if (!Stack || Stack->AnimPlayers.IsEmpty())
		{
			return false;
		}

		const FBlendStackAnimPlayer& Player = Stack->AnimPlayers[0];
		// 전신 Slot 뒤의 source 는 inactive 로 보일 수 있어 자산, 재생 개시, 혼합 완료를 직접 본다.
		if (Player.GetAnimationAsset() != Selected || Player.IsPostponed()
			|| Player.GetBlendInPercentage() < 1.f)
		{
			return false;
		}

		if (Player.GetBlendInWeightsNum() > 0)
		{
			TArray<float, TInlineAllocator<128>> Weights;
			Weights.SetNumUninitialized(Player.GetBlendInWeightsNum());
			Player.GetBlendInWeights(MakeArrayView(Weights));
			for (float Weight : Weights)
			{
				if (!FMath::IsFinite(Weight) || Weight < 1.f)
				{
					return false;
				}
			}
		}

		return true;
	}
}

bool FVBAnimGuardHandoff::CanUseGuardPose(const UAnimInstance& Anim, const FPoseGate& Gate)
{
	const AVBCharacter* Character = Cast<AVBCharacter>(Anim.TryGetPawnOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Gate.bEnableGuardTurnPose || !Gate.bUseGASPLocomotion || Gate.bPoseOwnedByMontage || Gate.bRagdollPowered || Gate.bUseSlidePose
		|| !Movement || !Movement->IsMovingOnGround() || Movement->IsCrouching())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	const UVBWeaponDefinition* Definition = Gate.WeaponState ? Gate.WeaponState->GetSelectedDefinition() : nullptr;
	const FVBGuardSet* Set = Definition ? &Definition->GetGuardSet() : nullptr;
	const bool bMoving = !Movement->GetCurrentAcceleration().IsNearlyZero()
		|| Movement->Velocity.SizeSquared2D() > FMath::Square(Gate.MoveThreshold);
	if (!Set || !Set->AllowsGuardLowerBody(bMoving))
	{
		return false;
	}

	if (!ASC || !ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Guarding)
		|| !Set || !Set->GuardLocomotionChooser || !Set->GuardMontage || !Anim.Montage_IsPlaying(Set->GuardMontage)
		|| Anim.Montage_GetCurrentSection(Set->GuardMontage) != Set->GuardLoopSection)
	{
		return false;
	}

	// 다른 전신 몽타주의 블렌드아웃까지 끝나야 하체를 돌려받는다.
	for (const FAnimMontageInstance* Instance : Anim.MontageInstances)
	{
		if (Instance && Instance->Montage != Set->GuardMontage
			&& (Instance->IsActive() || Instance->GetWeight() > ZERO_ANIMWEIGHT_THRESH))
		{
			return false;
		}
	}

	return true;
}

UChooserTable* FVBAnimGuardHandoff::FindGuardChooser(const UVBWeaponStateComponent* WeaponState)
{
	const UVBWeaponDefinition* Definition = WeaponState ? WeaponState->GetSelectedDefinition() : nullptr;
	const FVBGuardSet* Set = Definition ? &Definition->GetGuardSet() : nullptr;
	return Set ? Set->GuardLocomotionChooser.Get() : nullptr;
}

bool FVBAnimGuardHandoff::Update(const UChooserTable* GuardChooser, UAnimInstance* LocomotionGraph)
{
	// 가드 선택기의 새 선택이 혼합까지 끝나기 전에는 DefaultSlot 가드 자세가 전신 포즈를 점유한다.
	const VBGASP::FStateView GuardView(LocomotionGraph);
	const FVBGASPCMCState* GuardState = GuardView.NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
	const bool bGuardSelectionReady = GuardChooser && GuardState && GuardState->ActiveChooser == GuardChooser
		&& GuardState->bHasSelection && !GuardState->bSelectionContractFailed && !GuardState->bWaitingForPhase;
	if (!bGuardSelectionReady || BlendReadyChooser.Get() != GuardChooser)
	{
		BlendReadyChooser.Reset();
	}

	if (bGuardSelectionReady && !BlendReadyChooser.IsValid())
	{
		const FVBGASPCMCBlendInputs* Inputs = GuardView.NativePtr<FVBGASPCMCBlendInputs>(TEXT("CMCBlendInputs"));
		if (Inputs && IsGuardBaseBlendComplete(LocomotionGraph, Inputs->Anim))
		{
			BlendReadyChooser = const_cast<UChooserTable*>(GuardChooser);
		}
	}

	return bGuardSelectionReady && BlendReadyChooser.Get() == GuardChooser;
}

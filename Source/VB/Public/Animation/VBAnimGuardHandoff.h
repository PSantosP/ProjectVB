// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UAnimInstance;
class UChooserTable;
class UVBWeaponStateComponent;

// 가드 유지 중 하체를 로코모션 그래프로 돌려줘도 되는지 정한다. UVBAnimInstance 가 값으로 들고 게임 스레드에서만 쓴다.
struct VB_API FVBAnimGuardHandoff
{
	// 가드 하체를 쓸 수 있는지 묻는 인스턴스 쪽 사실.
	struct FPoseGate
	{
		bool bEnableGuardTurnPose = false;
		bool bUseGASPLocomotion = false;
		bool bPoseOwnedByMontage = false;
		bool bRagdollPowered = false;
		bool bUseSlidePose = false;
		float MoveThreshold = 0.0f;
		const UVBWeaponStateComponent* WeaponState = nullptr;
	};

	// 가드 유지 구간이고 다른 전신 몽타주가 없어 가드 하체를 요청해도 되는가.
	static bool CanUseGuardPose(const UAnimInstance& Anim, const FPoseGate& Gate);
	// 지금 고른 무기의 가드 로코모션 chooser.
	static UChooserTable* FindGuardChooser(const UVBWeaponStateComponent* WeaponState);

	// 요청한 chooser 의 선택과 혼합이 끝났으면 true. 그 전에는 DefaultSlot 가드 자세가 전신 포즈를 점유한다.
	bool Update(const UChooserTable* GuardChooser, UAnimInstance* LocomotionGraph);
	// 혼합 완료 기록을 지운다.
	void Reset() { BlendReadyChooser.Reset(); }

private:
	// 혼합 완료를 확인한 chooser. 가드 선택기 진입 때만 실제 BlendStack 혼합 완료를 기다린다.
	TWeakObjectPtr<UChooserTable> BlendReadyChooser;
};

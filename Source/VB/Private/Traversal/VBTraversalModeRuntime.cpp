// Copyright VB. All Rights Reserved.

#include "Traversal/VBHangRuntime.h"
#include "Traversal/VBLadderRuntime.h"
#include "Traversal/VBClimbRuntime.h"
#include "Traversal/VBTraversalDiagnostics.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SplineComponent.h"
#include "HAL/IConsoleManager.h"
#include "Traversal/VBLadder.h"

static TAutoConsoleVariable<int32> CVarTraversalStatusTelemetry(
	TEXT("vb.Traversal.StatusTelemetry"), 0,
	TEXT("Climb and hang status: 0=off, 1=one status line per second and climb limb rate sampling every tick."),
	ECVF_Default);

bool VBTraversalDiagnostics::IsStatusTelemetryEnabled()
{
	return CVarTraversalStatusTelemetry.GetValueOnGameThread() > 0;
}

// 칸을 비우는 순서는 묶음으로 옮기기 전 그대로다 - 몽타주를 멈추기 전에 비우던 칸은 멈추기 전에 비운다.

void FVBHangRuntime::ClearOnExit(UAnimInstance* Anim, float StopBlend)
{
	Ledge = nullptr;
	MoveInput = FVector2D::ZeroVector;
	MoveAxis = 0.0f;
	bExitRequested = false;
	bExitInputArmed = false;
	IKAlpha = 0.0f;
	IKTargets = FVBHangIKTargets(); // 가중치 0 = ABP 의 IK 노드가 통째로 꺼진다
	if (ShimmyMontage)
	{
		if (Anim)
		{
			Anim->Montage_Stop(StopBlend, ShimmyMontage);
		}

		ShimmyMontage = nullptr;
	}

	ShimmyPosition = 0.0f;
}

void FVBLadderRuntime::ClearOnExit(UAnimInstance* Anim, float StopBlend)
{
	ActiveLadder = nullptr;
	Diag.bPlacedOnce = false;
	bMontageTracked = false;
	bSettling = false;
	ResumeMontage = nullptr;
	if (Anim && ActiveMontage)
	{
		Anim->Montage_Stop(StopBlend, ActiveMontage);
	}

	ActiveMontage = nullptr;
}

void FVBClimbRuntime::ClearOnExit(UAnimInstance* Anim, float StopBlend)
{
	MoveInput = FVector2D::ZeroVector;
	MoveAxis = FVector2D::ZeroVector;
	IKTargets = FVBHangIKTargets(); // 가중치 0 = ABP 의 IK 노드가 통째로 꺼진다
	bPlacedOnce = false;
	PlayRate = 1.0f;
	if (ActiveMontage)
	{
		if (Anim)
		{
			Anim->Montage_Stop(StopBlend, ActiveMontage);
		}

		ActiveMontage = nullptr;
		ActiveDir = FVector2D::ZeroVector;
		PendingMontage = nullptr;
	}
}

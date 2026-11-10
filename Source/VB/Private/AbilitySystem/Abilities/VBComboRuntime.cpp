// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBComboRuntime.h"

#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "Data/VBAttackConfig.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "VB/VB.h"

#if !UE_BUILD_SHIPPING
// 개발용 A/B - 접촉 뒤 이동 끊기를 새로 민 막대와 계속 민 막대로 자산 수정 없이 한 PIE 에서 비교한다
static TAutoConsoleVariable<int32> CVarWindowMoveCut(
	TEXT("vb.Combat.WindowMoveCut"), -1,
	TEXT("Dev test: combo-window move cut. -1 = asset value (bMoveCutAcceptsHeldStick), 0 = only a fresh push cuts, 1 = a held stick cuts too."),
	ECVF_Cheat);
#endif

namespace
{
	// 콘솔 스위치가 없는 Shipping 에서는 -1(자산 값 사용)
	int32 WindowMoveCutOverride()
	{
#if !UE_BUILD_SHIPPING
		return CVarWindowMoveCut.GetValueOnGameThread();
#else
		return -1;
#endif
	}
}

void FVBComboRuntime::ResetForActivation()
{
	ComboIndex = 0;
	bWindowOpen = false;
	bSaveAttack = false;
}

void FVBComboRuntime::PrepareFirstSwing()
{
	ComboInputTime = -1.0;
	bChainedThisWindow = false;
	ActiveWindowNotify.Reset();
	bHitFrameReached = false;
	HitFrameTime = -1.0;
}

bool FVBComboRuntime::IsFinalStrike(const UVBAttackConfig* Config) const
{
	return Config && ComboIndex == Config->MaxComboCount - 1;
}

void FVBComboRuntime::OpenWindow(const UObject* WindowNotify, const UVBAttackConfig* Config, double Now)
{
	ActiveWindowNotify = WindowNotify;
	bWindowOpen = true;
	bChainedThisWindow = false;

	// AI 자동 콤보: 공격 자산이 버튼 없이 잇는다고 하면 창이 열리는 순간이 곧 입력이다.
	// 아래 버퍼, 전환 규칙은 그대로 탄다 - 적과 플레이어의 콤보 타이밍 기준이 하나다.
	if (Config && Config->bAutoChainCombo && !bSaveAttack)
	{
		bSaveAttack = true;
		VB_LOG(Log, "[Combo] 자동 잇기 - %d타 창 열림", ComboIndex + 1);
	}

	// 버퍼 소비: 창이 열리기 전에 눌린 입력은 나이가 버퍼 안이면 지금 쓴다.
	if (ComboInputTime >= 0.0 && !bSaveAttack)
	{
		const double Age = Now - ComboInputTime;
		const float Buffer = Config ? Config->ComboInputBufferSeconds : 0.0f;
		if (Age <= Buffer)
		{
			bSaveAttack = true;
			VB_LOG(Log, "[Combo] 버퍼 소비 - 입력 나이 %.2f초 (버퍼 %.2f)", Age, Buffer);
		}
		else
		{
			VB_LOG(Log, "[Combo] 버퍼 만료 - 입력 나이 %.2f초 > %.2f, 버림", Age, Buffer);
			ComboInputTime = -1.0;
			PendingChainIntent = FVector2D::ZeroVector; // 버린 누름의 방향이 자동 잇기로 새지 않게
		}
	}
}

bool FVBComboRuntime::CloseWindow(const UObject* WindowNotify, float EventMagnitude)
{
	// 점프가 옛 창의 끝을 늦게 보내도 새 창의 입력과 상태를 소비하지 않는다.
	if (WindowNotify)
	{
		if (WindowNotify != ActiveWindowNotify.Get())
		{
			return false;
		}

		ActiveWindowNotify.Reset();
		if (EventMagnitude <= 0.0f)
		{
			bWindowOpen = false;
			return false; // 취소된 창은 다음 공격으로 잇거나 복귀를 취소할 수 없다.
		}
	}

	bWindowOpen = false;

	// 섹션 점프가 낸 옛 창의 닫힘 - 이미 다음 타로 넘어갔으므로 아무것도 하지 않는다.
	if (bChainedThisWindow)
	{
		bChainedThisWindow = false;
		return false;
	}

	return true;
}

bool FVBComboRuntime::NoteInput(const FVector2D& Intent, double Now)
{
	PendingChainIntent = Intent;

	// 입력 버퍼: 창과 무관하게 시각을 기억한다. 창이 열려 있으면 즉시 저장하고, 아니면 열릴 때 나이로 판정한다.
	ComboInputTime = Now;
	if (bWindowOpen)
	{
		bSaveAttack = true;
		return true;
	}

	return false;
}

void FVBComboRuntime::NoteHitFrame(double Now)
{
	bHitFrameReached = true;
	HitFrameTime = Now;
}

bool FVBComboRuntime::CanChainAfterHit(const UVBAttackConfig* Config) const
{
	// 네 조건: 저장 입력 / 조기 전환 스위치 / 창 열림 / 닿음.
	if (!bSaveAttack || !Config || !Config->bChainAtWindowOpen)
	{
		return false;
	}

	return bWindowOpen && bHitFrameReached;
}

double FVBComboRuntime::ChainDelayRemaining(const UVBAttackConfig& Config, double Now) const
{
	return (HitFrameTime + Config.ChainDelayAfterHitSeconds) - Now;
}

void FVBComboRuntime::ScheduleChainRetry(UWorld* World, const FTimerDelegate& OnRetry, double Remaining)
{
	if (World)
	{
		World->GetTimerManager().SetTimer(ChainDelayTimer, OnRetry, static_cast<float>(Remaining), false);
	}
}

bool FVBComboRuntime::BeginChain(const UVBAttackConfig* Config, const UAnimMontage* Montage, UWorld* World)
{
	if (!bSaveAttack)
	{
		return false;
	}

	// 마지막 타는 설정의 최대 타 수와 그 몽타주에 다음 섹션이 있는지 둘 다로 정한다.
	// 한 구간짜리 질주 공격은 공통 설정의 4타를 믿으면 없는 섹션으로 뛴다.
	if (!Config || ComboIndex >= Config->MaxComboCount - 1
		|| !VBMeleeAttackRules::HasComboSection(Montage, Config, ComboIndex + 1))
	{
		bSaveAttack = false; // 마지막 타 뒤의 입력은 버린다 - 다음 활성화가 처음부터 시작
		ComboInputTime = -1.0;
		PendingChainIntent = FVector2D::ZeroVector; // 일어나지 않을 타의 방향으로 대상을 바꾸지 않는다
		return false;
	}

	bChainedThisWindow = true; // 섹션 점프가 옛 창의 닫힘을 동기로 낼 수 있으므로 먼저 세운다
	ActiveWindowNotify.Reset();
	bWindowOpen = false;
	bHitFrameReached = false; // 다음 섹션의 닿음은 아직
	HitFrameTime = -1.0;
	if (World)
	{
		World->GetTimerManager().ClearTimer(ChainDelayTimer);
	}

	return true;
}

void FVBComboRuntime::FinishChain()
{
	bSaveAttack = false;
	ComboInputTime = -1.0;
}

void FVBComboRuntime::StartMoveCutWatch(const UVBAttackConfig* Config, UWorld* World, const FTimerDelegate& OnTick)
{
	if (!Config || !Config->bMoveCancelsRecoveryInWindow || !bWindowOpen)
	{
		return;
	}

	WindowMoveGate.Reset();
	if (World)
	{
		World->GetTimerManager().SetTimer(WindowMoveCutTimer, OnTick, FVBMoveCancelGate::Interval, true);
	}
}

bool FVBComboRuntime::StepMoveCut(const UVBAttackConfig& Config, bool bHasMove, bool& bOutAcceptsHeld)
{
	// 판정 문은 계속 민 막대 설정에서도 매번 지난다 - 설정을 도중에 끄면 새로 밀었는지가 그 자리에서 바로 이어져야 한다.
	const bool bFreshPush = WindowMoveGate.Step(bHasMove);
	bOutAcceptsHeld = ResolveMoveCutAcceptsHeld(Config.bMoveCutAcceptsHeldStick, WindowMoveCutOverride());
	return ShouldCutRecoveryOnMove(Config.bMoveCancelsRecoveryInWindow, bWindowOpen, bSaveAttack,
		bOutAcceptsHeld ? bHasMove : bFreshPush);
}

void FVBComboRuntime::StopMoveCutWatch(UWorld* World)
{
	if (World)
	{
		World->GetTimerManager().ClearTimer(WindowMoveCutTimer);
	}
}

void FVBComboRuntime::StopTimers(UWorld* World)
{
	if (World)
	{
		World->GetTimerManager().ClearTimer(ChainDelayTimer);
	}

	StopMoveCutWatch(World);
}

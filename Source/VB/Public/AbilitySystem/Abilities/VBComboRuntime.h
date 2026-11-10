// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/VBMoveCancelGate.h" // FVBMoveCancelGate - 창 안 이동 끊기 판정을 값으로 든다
#include "TimerManager.h" // FTimerDelegate - 능력이 전달하는 타이머 콜백을 값으로 받는다

class UAnimMontage;
class UObject;
class UVBAttackConfig;
class UWorld;

/** 근접 공격 한 번의 콤보 상태(타 번호, 콤보 창, 입력 버퍼, 전환 타이머)를 갖고 다음 타 전환과 회복 끊기 여부를 판정한다. */
struct VB_API FVBComboRuntime
{
	// 회복을 끊으면 true. 자산이 켰고 창이 열렸고 공격 입력이 없고 막대를 새로 밀었을 때
	static bool ShouldCutRecoveryOnMove(bool bEnabled, bool bWindowOpen, bool bAttackSaved, bool bFreshPush)
	{
		return bEnabled && bWindowOpen && !bAttackSaved && bFreshPush;
	}

	// 계속 민 막대도 세면 true - 콘솔 값(-1 자산 / 0 / 1)이 자산 값을 덮는다.
	static bool ResolveMoveCutAcceptsHeld(bool bAssetValue, int32 ConsoleValue)
	{
		return ConsoleValue == 0 ? false : (ConsoleValue == 1 ? true : bAssetValue);
	}

	// 새 활성화가 시작될 때 타 번호, 창, 저장 입력을 비운다.
	void ResetForActivation();
	// 첫 타를 재생하기 직전에 입력 시각, 창 플래그, 적중 상태를 비운다.
	void PrepareFirstSwing();
	// 회피 재개로 시작할 타를 정한다.
	void ResumeAtStrike(int32 ComboIndexToResume) { ComboIndex = ComboIndexToResume; }

	// 지금 타 번호(0 = 첫 타)
	int32 GetComboIndex() const { return ComboIndex; }
	bool IsWindowOpen() const { return bWindowOpen; }
	// 다음 타로 넘어갈 입력이 저장되어 있으면 true
	bool IsAttackSaved() const { return bSaveAttack; }
	// 이 섹션의 닿는 프레임이 지났으면 true
	bool IsHitFrameReached() const { return bHitFrameReached; }
	bool IsFinalStrike(const UVBAttackConfig* Config) const;
	// 끊긴 뒤 재개할 타. 안 닿았으면 그 타, 닿았으면 다음 타다.
	int32 ResumeStrikeAfterCut() const { return bHitFrameReached ? ComboIndex + 1 : ComboIndex; }

	// 서버가 콤보 중 받은 누름의 방향. 다음 섹션으로 넘어갈 때 한 번 쓴다. 자동 연결은 이 값이 없어 대상을 바꾸지 않는다
	const FVector2D& GetChainIntent() const { return PendingChainIntent; }
	void ClearChainIntent() { PendingChainIntent = FVector2D::ZeroVector; }

	// 콤보 창을 열고 자동 잇기와 입력 버퍼를 처리한다.
	void OpenWindow(const UObject* WindowNotify, const UVBAttackConfig* Config, double Now);
	// 창 닫힘을 처리한다. 뒤이은 전환이나 회복 끊기를 능력이 진행해야 하면 true.
	bool CloseWindow(const UObject* WindowNotify, float EventMagnitude);
	// 공격 입력의 방향과 시각을 기억한다. 창이 열려 있어 입력이 저장되면 true.
	bool NoteInput(const FVector2D& Intent, double Now);
	void NoteHitFrame(double Now);

	// 저장 입력, 조기 전환 설정, 창, 닿음이 다 맞으면 true.
	bool CanChainAfterHit(const UVBAttackConfig* Config) const;
	// 닿은 뒤 남은 여운(초). 0 이하면 지금 넘어갈 수 있다.
	double ChainDelayRemaining(const UVBAttackConfig& Config, double Now) const;
	// 여운이 끝나면 OnRetry 를 부른다.
	void ScheduleChainRetry(UWorld* World, const FTimerDelegate& OnRetry, double Remaining);

	// 다음 타로 넘어갈 수 있으면 창 상태를 비우고 true. 다음 타가 없으면 저장 입력을 버리고 false.
	bool BeginChain(const UVBAttackConfig* Config, const UAnimMontage* Montage, UWorld* World);
	void AdvanceStrike() { ++ComboIndex; }
	// 전환이 끝난 뒤 저장 입력과 입력 시각을 비운다
	void FinishChain();
	// 창 닫힘 자체가 전환을 일으켰을 때 지운다 - 그 창의 닫힘은 다시 오지 않는다.
	void ClearChainedMark() { bChainedThisWindow = false; }

	// 설정이 켰고 창이 열려 있으면 창 안 이동 끊기 검사를 시작한다.
	void StartMoveCutWatch(const UVBAttackConfig* Config, UWorld* World, const FTimerDelegate& OnTick);
	// 한 번 검사한다. 회복을 끊어야 하면 true.
	bool StepMoveCut(const UVBAttackConfig& Config, bool bHasMove, bool& bOutAcceptsHeld);
	void StopMoveCutWatch(UWorld* World);
	// 전환 대기와 이동 끊기 검사를 모두 멈춘다. 능력이 끝날 때 부른다.
	void StopTimers(UWorld* World);

private:
	int32 ComboIndex = 0;
	bool bWindowOpen = false;
	bool bSaveAttack = false;

	// 마지막 콤보 입력 시각(월드 초, -1 = 없음). 창이 열릴 때 나이로 소비와 폐기를 가른다.
	double ComboInputTime = -1.0;
	// 이 창에서 이미 다음 타로 전환했다는 플래그 - 섹션 점프가 내는 이전 창 닫힘을 한 번 무시한다.
	bool bChainedThisWindow = false;
	TWeakObjectPtr<const UObject> ActiveWindowNotify;
	FVector2D PendingChainIntent = FVector2D::ZeroVector;

	// 전환은 창 열림과 닿음 중 늦은 쪽 뒤에만 한다.
	bool bHitFrameReached = false;
	double HitFrameTime = -1.0;
	FTimerHandle ChainDelayTimer;
	FVBMoveCancelGate WindowMoveGate;
	FTimerHandle WindowMoveCutTimer;
};

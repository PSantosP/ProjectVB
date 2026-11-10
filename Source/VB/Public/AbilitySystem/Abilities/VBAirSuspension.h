// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

class UCharacterMovementComponent;
struct FVBKnockdownSet;

// 체공 감시 한 주기의 결론.
enum class EVBAirWatchResult : uint8
{
	Stay,
	Landed,
	NeverLeftGround,
	BudgetSpent,
};

// 띄워진 피해자의 공중 정지. 체공 시계와 한도, 공격자의 붙잡기, 내려찍기 진입, 중력 대여를 맡는다.
// 요청 검증, 단계, 몽타주는 UVBGA_Knockdown 이 맡는다. 빌린 중력은 빌린 이동 컴포넌트에만 돌려준다.
struct VB_API FVBAirSuspension
{
	// 새 띄움을 시작한다. 시계와 한도를 새로 잡는다.
	void Begin(float MaxAirSeconds);

	// 공격자의 붙잡기 요청. 이번 띄움의 첫 요청이고 시간이 유효하면 한도를 늘리고 true.
	bool TryHold(float AttackerMaxAirSeconds);

	// 이 낙하 속도와 착지 한도로 내려찍을 수 있으면 true.
	bool CanSlam(float FallSpeed, float SlamMaxSeconds) const;

	// 붙잡기를 풀고 아래로 민다. 연타 체공 한도와 별도로 착지할 시간을 확보한다.
	void Slam(UCharacterMovementComponent& Move, float FallSpeed, float SlamMaxSeconds);

	// 공중 피격 한 대. 붙잡혀 있지 않으면 한도를 늘리고 위로 받친 뒤 true.
	bool HoldUpAfterHit(const FVBKnockdownSet& Set, UCharacterMovementComponent* Move);

	// 붙잡힌 채 내려오기 시작하면 중력을 빌려 공중에 세운다.
	void FreezeIfHeld(UCharacterMovementComponent& Move, bool bFalling);

	// 빌린 중력을 돌려주고 붙잡기를 푼다. 다시 불러도 안전하다.
	void Release();

	// 시계를 진행하고 체공을 끝낼지 답한다. 내려찍기 반응이 도는 동안은 한도 안에서 착지를 미룬다.
	EVBAirWatchResult AdvanceWatch(float DeltaSeconds, bool bFalling, bool bSlamReactionPlaying, float TakeoffGraceSeconds);

	float GetElapsedSeconds() const { return ElapsedSeconds; }
	float GetBudgetSeconds() const { return BudgetSeconds; }
	bool IsHeld() const { return bHoldRequested; }
	bool IsGravityBorrowed() const { return RestoreGravity.IsSet(); }

private:
	float ElapsedSeconds = 0.0f;
	float BudgetSeconds = 0.0f;
	// 띄움 속도는 능력이 켜진 뒤 같은 프레임에 와서 처음에는 아직 땅에 있다.
	bool bLeftGround = false;
	bool bHoldRequested = false;
	TOptional<float> RestoreGravity;
	TWeakObjectPtr<UCharacterMovementComponent> GravityOwner;
};

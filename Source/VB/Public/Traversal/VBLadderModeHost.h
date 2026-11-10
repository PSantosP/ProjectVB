// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Traversal/VBTraversalRules.h"

class AActor;
class AVBCharacter;
class AVBLadder;
class UActorComponent;
class USkeletalMeshComponent;
class UVBClimbAnimSet;
class UVBTraversalConfig;
struct FTickFunction;

// 사다리 탑승 중 순서를 거는 틱 함수들. Owner, IntentTick, PlacementTick 은 호스트 소유라 모드보다 오래 산다.
struct FVBLadderTickTargets
{
	UObject* Owner = nullptr; // 호스트 틱 함수 쪽 의존 관계의 키
	FTickFunction* IntentTick = nullptr; // A 단계
	FTickFunction* PlacementTick = nullptr; // B 단계
	AActor* Controller = nullptr; // 시뮬레이티드 프록시에서는 null
	UActorComponent* Movement = nullptr;
	USkeletalMeshComponent* Mesh = nullptr;
};

// 사다리 모드가 소유 컴포넌트에 요청하는 기능 전체. 시험이 기록 호스트로 바꿔 끼우려고 인터페이스로 둔다.
//   Send* 는 권한에 따라 RPC 를 고르고, Confirm* 는 서버 전용이며, 받는 쪽은 적용만 하므로 다시 전송하지 않는다.
class IVBLadderModeHost
{
public:
	virtual AVBCharacter* GetCharacter() const = 0;
	virtual const UVBClimbAnimSet* GetClips() const = 0;
	virtual const UVBTraversalConfig* GetConfig() const = 0;

	// 사다리에 붙었는지는 모드만 쓴다. Start 가 켜고 ClearRiding 이 끈다.
	virtual void SetAttached(bool bAttached) = 0;
	// Falling 으로 세션을 복구한다.
	virtual void EndSessionFalling() = 0;
	virtual void RequireFreshAutoIntent() = 0;
	virtual bool IsRidingLadder() const = 0;
	// 매달림, 등반, 사다리 중이면 true. 클라이언트 탐색은 이 상태에서 사다리를 시작하지 않는다.
	virtual bool IsInSurfaceMode() const = 0;
	// 한 번짜리 동작을 포함해 트래버설 세션 중이면 true. 서버는 이때 시작 요청을 거절한다.
	virtual bool IsInAnyTraversal() const = 0;
	virtual bool IsBodyBusyForTraversal() const = 0;

	virtual void SendLadderStartRequest(AVBLadder* Ladder, float Height) = 0;
	virtual void SendLadderMoveInput(float InputY) = 0;
	virtual void SendLadderDropRequest() = 0;
	virtual void ConfirmLadderDrop() = 0;
	virtual void ConfirmLadderTopOut(const FVector& LedgePoint, const FVector& LedgeNormal) = 0;

	virtual FVBTopOutResult FindTopOutStand(const FVector& Edge, const FVector& OutwardNormal, const FFloatInterval& Band) const = 0;
	virtual void PlayTopOutMontage(const FVector& LedgePoint, const FVector& LedgeNormal, const TCHAR* From) = 0;

	virtual FVBLadderTickTargets GetLadderTickTargets() = 0;

protected:
	~IVBLadderModeHost() = default;
};

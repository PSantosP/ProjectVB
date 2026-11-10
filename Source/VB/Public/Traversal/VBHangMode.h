// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Traversal/VBHangRuntime.h"
#include "VBHangMode.generated.h"

class USplineComponent;
class UVBTraversalComponent;
struct FVBTraversalSession;

// 렛지 매달림 한 번의 처리: 잡기 판정, 시작, 틱 갱신, 옆걸음, 올라서기, 놓기.
//   소유 컴포넌트는 RPC 를 맡는다. 구현이 하나뿐이라 인터페이스 없이 소유 컴포넌트를 직접 부른다.
USTRUCT()
struct VB_API FVBHangMode
{
	GENERATED_BODY()

	// 트레이스가 맞은 블록이 항상 매달림으로 표시돼 있으면 true. 지상, 공중 두 갈림길이 같은 판정을 쓴다.
	static bool IsAlwaysHangBlock(const FVBTraversalCheckResult& Result);
	// 공중 잡기가 맨틀 대신 매달림으로 갈지. 블록이 항상 매달림이거나 렛지 아래 맨틀 높이 안에 바닥이 없으면 true.
	bool ShouldHangInsteadOfMantle(const UVBTraversalComponent& Owner, const FVBTraversalCheckResult& Result, bool bFrontFloorFound) const;
	// 손이 닿는 렛지면 매달림을 요청하고 true. 아직 멀면 false 로 다음 틱에 다시 본다.
	bool TryGrabLedge(UVBTraversalComponent& Owner, FVBTraversalCheckResult& Result);

	// 전원에서 실행되는 시작. 지난 상태를 비우고 렛지에 붙는다.
	void Start(UVBTraversalComponent& Owner, FVBTraversalSession& Session, const FVBTraversalCheckResult& Result);
	// 옆걸음 진행량만큼 스플라인 거리를 밀고, 거기서 캡슐 트랜스폼을 파생한다.
	void Update(UVBTraversalComponent& Owner, float DeltaTime);
	// 렛지에서 떨어져 올라서기 몽타주로 넘어간다. 재료가 없으면 놓는다.
	void StartClimbUp(UVBTraversalComponent& Owner, FVBTraversalSession& Session);
	// 매달려 있으면 놓고 Falling 으로 복구한다.
	void EndByDrop(UVBTraversalComponent& Owner);
	// 복구 때 밖이 읽는 칸을 비우고 옆걸음 몽타주를 멈춘다.
	void ClearOnExit(const UVBTraversalComponent& Owner);

	// 소유 클라 입력. X=좌우 옆걸음, Y=앞(올라서기)/뒤(놓기).
	void SetMoveInput(UVBTraversalComponent& Owner, const FVector2D& Input);
	// 입력 RPC 의 서버 쪽. 적용만 한다.
	void ApplyMoveInputX(float InputX);
	void RequestClimbUp(UVBTraversalComponent& Owner);
	void RequestDrop(UVBTraversalComponent& Owner);

	const FVBHangRuntime& GetState() const { return State; }
	const FVBHangLongLivedDiagnostics& GetLongLivedDiag() const { return LongLivedDiag; }

private:
	// 렛지 점, 법선으로 매달림 캡슐 월드 트랜스폼을 만든다.
	FTransform ComputeCapsuleTransform(const UVBTraversalComponent& Owner, const FVector& LedgePoint, const FVector& LedgeNormal) const;
	// 옆걸음 입력과 루트모션으로 스플라인 거리와 블렌드스페이스 축을 갱신한다.
	void AdvanceAlongLedge(UVBTraversalComponent& Owner, const USplineComponent& Spline, const FVector& LedgeNormal, float DeltaTime);
	// 몸통 깊이와 높이를 목표로 보간한다.
	void UpdateDepth(const UVBTraversalComponent& Owner, float DeltaTime);
	// 스플라인에서 파생한 자리에 캡슐을 놓는다.
	void PlaceCapsule(const UVBTraversalComponent& Owner, const USplineComponent& Spline, const FVector& LedgeNormal);
	// 매달림 상태와 접지를 1초에 한 번 찍는다. vb.Traversal.StatusTelemetry 가 켜졌을 때만 불린다.
	void LogStatus(const UVBTraversalComponent& Owner, const USplineComponent& Spline, const FVector& LedgeNormal);
	// 손은 렛지 스플라인, 발은 벽 표면을 IK 목표로 삼는다. 벽을 못 찾은 발은 가중치 0.
	void UpdateIKTargets(const UVBTraversalComponent& Owner, const FVector& LedgePoint, const FVector& LedgeNormal, float DeltaTime);
	// 옆걸음 몽타주를 이 틱만큼 돌린다. 몽타주 없이 갈 때의 진행량(cm, 부호 없음)을 돌려주고 몽타주가 돌면 0.
	float TickShimmyMontage(const UVBTraversalComponent& Owner, float DeltaTime, float InputX);

	UPROPERTY()
	FVBHangRuntime State;

	FVBHangLongLivedDiagnostics LongLivedDiag;
};

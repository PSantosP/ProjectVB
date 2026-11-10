// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Traversal/VBClimbRuntime.h"
#include "Traversal/VBTraversalRules.h"
#include "VBClimbMode.generated.h"

class UAnimMontage;
class UPrimitiveComponent;
class UVBTraversalComponent;
struct FHitResult;
struct FVBTraversalSession;

// 자유 등반 한 번의 처리: 면 찾기, 시작, 틱 갱신, 몽타주 선택, 말단 IK, 꼭대기 올라서기, 놓기.
// 소유 컴포넌트는 RPC 를 맡고, 구현이 하나뿐이라 인터페이스 없이 직접 부른다.
USTRUCT()
struct VB_API FVBClimbMode
{
	GENERATED_BODY()

	// 바라보는 쪽에 등반 가능한 면이 있으면 붙는다. 렛지 판정이 실패한 자리에서 불린다
	bool TryStart(UVBTraversalComponent& Owner);
	// 전원에서 실행되는 시작. 지난 상태를 비우고 면에 붙는다
	void Start(UVBTraversalComponent& Owner, FVBTraversalSession& Session, const FVector& SurfacePoint, const FVector& SurfaceNormal,
		UPrimitiveComponent* Surface);
	void Update(UVBTraversalComponent& Owner, float DeltaTime);
	// 등반 중이면 놓고 Falling 으로 복구한다
	void EndByDrop(UVBTraversalComponent& Owner);
	// 등반 입력, 축, IK, 몽타주를 걷는다. 복구와 꼭대기 올라서기가 같이 쓴다
	void ClearOnExit(UVBTraversalComponent& Owner);
	// 등반만 걷고 트래버설 상태는 몽타주 끝 복구까지 유지한다
	void StartTopOut(UVBTraversalComponent& Owner, const FVector& LedgePoint, const FVector& LedgeNormal);

	// 소유 클라의 이동 입력. X=좌우, Y=위아래
	void SetMoveInput(UVBTraversalComponent& Owner, const FVector2D& Input);
	// 입력 RPC 의 서버 쪽. 적용만 한다
	void ApplyMoveInput(const FVector2D& Input);
	void RequestDrop(UVBTraversalComponent& Owner);

	// SurfacePoint 에서 Side 쪽(음수 왼쪽) 손발 높이에 면이 있으면 true
	bool HasFootprintAt(const UVBTraversalComponent& Owner, const FVector& SurfacePoint, const FVector& Normal, float Side) const;

	const FVBClimbRuntime& GetState() const { return State; }
	const FVBClimbLongLivedDiagnostics& GetLongLivedDiag() const { return LongLivedDiag; }

private:
	// 등반 한 틱의 튐 진단 재료. 틱 시작 값과 이동 단계가 쓴 값을 배치 단계까지 나른다
	struct FTickMotion
	{
		FVector SurfaceBefore = FVector::ZeroVector;
		float DepthBefore = 0.0f;
		FVector RootMotionMoved = FVector::ZeroVector;
		float AppliedRight = 0.0f;
		float AppliedUp = 0.0f;
	};

	// 한 점 둘레에서 등반 면을 찾는다. 충분히 서 있는 면만 true
	bool TraceSurface(const UVBTraversalComponent& Owner, const FVector& Around, const FVector& Normal, FHitResult& OutHit) const;
	// 벽 위에 올라설 자리를 찾는다(서버). 못 찾으면 결과에 거절 이유
	FVBTopOutResult FindTopOut(const UVBTraversalComponent& Owner) const;
	// 갈 수 없는 성분을 입력에서 지운다. 꼭대기 올라서기가 시작됐으면 true
	bool FilterInput(UVBTraversalComponent& Owner, const FVector& PlaneRight, FVector2D& InOutInput);
	// 서버가 윗면을 찾으면 올라서기를 퍼뜨리고 true
	bool TryTopOut(UVBTraversalComponent& Owner);
	// 루트모션 또는 일정 속도로 면 위 자리를 옮기고 축을 갱신한다
	void MoveAlongSurface(const UVBTraversalComponent& Owner, const FVector2D& EffectiveInput, const FVector& PlaneRight, float DeltaTime,
		FTickMotion& Motion);
	// 한 틱 이동을 상한으로 자른 값을 돌려준다
	FVector ClampStep(const UVBTraversalComponent& Owner, const FVector& RootMotionMoved, float DeltaTime);
	// 말단의 캐릭터 기준 최고 속도를 잰다. vb.Traversal.StatusTelemetry 가 켜졌을 때만 불린다
	void TrackLimbRate(const UVBTraversalComponent& Owner, float DeltaTime);
	// 등반 상태를 1초에 한 번 찍는다. vb.Traversal.StatusTelemetry 가 켜졌을 때만 불린다
	void LogStatus(const UVBTraversalComponent& Owner);
	// 면 위 자리에 캡슐을 놓는다. 진입 중이면 블렌드한다
	void PlaceCapsule(const UVBTraversalComponent& Owner, float DeltaTime, const FTickMotion& Motion);
	// 보이는 자리가 한 틱에 문턱을 넘게 뛰었으면 항목별로 찍는다
	void ReportJump(const UVBTraversalComponent& Owner, const FVector& Location, const FVector& EnterPull, float DeltaTime,
		const FTickMotion& Motion);
	// 면 위의 점과 법선으로 캡슐 월드 트랜스폼을 만든다
	FTransform ComputeCapsuleTransform(const UVBTraversalComponent& Owner, const FVector& SurfacePoint, const FVector& SurfaceNormal) const;
	// 네 말단 IK 목표를 말단 자리에서 벽으로 쏴 정한다. 보정량은 잰 오차만큼 누적해 고친다
	void UpdateIKTargets(const UVBTraversalComponent& Owner, float DeltaTime);
	// 입력 방향에 맞는 몽타주. 없으면 nullptr
	UAnimMontage* PickMontage(const UVBTraversalComponent& Owner, const FVector2D& Input, FVector2D& OutDir) const;
	// 등반 몽타주를 이 틱만큼 돌리고, 지금 걸린 몽타주가 맡은 8방향 단위벡터를 돌려준다(이동은 루트모션).
	FVector2D TickMontage(const UVBTraversalComponent& Owner, const FVector2D& Input);

	UPROPERTY()
	FVBClimbRuntime State;

	FVBClimbLongLivedDiagnostics LongLivedDiag;
};

#pragma once

#include "Camera/CameraTypes.h"
#include "Camera/VBEnvironmentCameraPolicy.h"

class ACharacter;
class APlayerController;
class UCapsuleComponent;
class UVBEnvironmentCameraConfig;

// 환경 카메라의 위치 풀이(후보, 충돌, 회복). AVBPlayerCameraManager 가 들고 UpdateViewTarget 에서 돌린다.
//   시험이 VBTests 모듈에 있어 export 한다.
class VB_API FVBEnvironmentCameraSolver final
{
public:
	void Reset();
	void Update(ACharacter* Pawn, const UVBEnvironmentCameraConfig* Settings, float DeltaTime, bool bCameraCut, FMinimalViewInfo& Pose);
private:
	struct FSolveFrame;
	// 이번 프레임의 기하와 충돌 질의를 모은다.
	static FSolveFrame BuildSolveFrame(ACharacter& Pawn, const APlayerController& PC, const UCapsuleComponent& Capsule,
		const UVBEnvironmentCameraConfig& Settings, const FMinimalViewInfo& Pose);
	// 기준점을 안전한 자리로 물린다. 갈 곳이 없으면 Pose 를 안전한 자리로 옮기고 false 다.
	bool SecureAnchor(FSolveFrame& Frame, const ACharacter& Pawn, FMinimalViewInfo& Pose);
	// 고른 목표 쪽으로 로컬 오프셋을 옮긴다. 구도 조정이 막힌 캐릭터 상태면 0 이다.
	void UpdateLocalAdjustment(const FSolveFrame& Frame, const FVector& Target, bool bObstructed, float DeltaTime);
	// 오프셋과 거리 회복을 반영한 최종 위치를 안전하게 잘라 돌려준다.
	FVector SolveFinalPosition(const FSolveFrame& Frame, const FVector& Target, bool bObstructed, float DeltaTime);
	// 디버그 콘솔 스위치가 켜졌을 때 상태 변화를 남기고 카메라를 그린다.
	void ReportDebug(const FSolveFrame& Frame, const ACharacter& Pawn, bool bWasConstrained, int32 Selected, double DesiredLength,
		const FVector& FinalSafe) const;

	VBEnvironmentCameraPolicy::FRecovery Recovery;
	FVector LastAnchor = FVector::ZeroVector;
	FVector LastPosition = FVector::ZeroVector;
	FVector LocalAdjustment = FVector::ZeroVector;
	int32 LastCandidate = INDEX_NONE;
	// 마지막으로 푼 엔진 프레임 - 건너뛴 프레임(다른 뷰 대상만 보던 동안)이 있으면 기록은 낡았다.
	uint64 LastSolvedFrame = 0;
	bool bHasHistory = false;
	bool bWasUnresolved = false;
};

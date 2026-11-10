// Copyright VB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VB/VB.h"
#include "Traversal/VBTraversalTypes.h"
#include "VBClimbRuntime.generated.h"

class UAnimInstance;
class UAnimMontage;

// 등반의 진단 줄 간격과 튐 계측. 게임 동작에는 쓰지 않는다.
struct FVBClimbDiagnostics
{
	// 지난 1초의 캐릭터, 말단(캐릭터 기준) 최대 속도(cm/s).
	float MaxBodyRate = 0.0f;
	float MaxLimbRate = 0.0f;
	FVector PrevLimbRel[4] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	bool bLimbRatePrimed = false;
	// 로그 간격(사다리 진단과 같다).
	FVBLogGate StatusLog{1.0};
	FVBLogGate ClampLog{1.0};
	FVBLogGate JumpLog{0.05};
	FVBLogGate TopOutLog{0.5};
};

// 등반 한 번보다 오래 사는 진단. 시작이 비우지 않는다.
struct FVBClimbLongLivedDiagnostics
{
	FVBLogGate ProbeLog{0.5};
};

// 자유 등반 한 번의 실행 상태. FVBClimbMode::Start 가 통째로 비운다.
USTRUCT()
struct VB_API FVBClimbRuntime
{
	GENERATED_BODY()

	// 붙어 있는 면 위의 점(root 높이)과 바깥 법선. 캡슐 자리를 정한다.
	FVector SurfacePoint = FVector::ZeroVector;
	FVector SurfaceNormal = FVector::ForwardVector;
	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector2D MoveAxis = FVector2D::ZeroVector;
	// 면에서 root 까지의 깊이.
	float Depth = 0.0f;
	FVBHangIKTargets IKTargets;

	// 지금 도는 등반 몽타주. 있으면 이동은 루트모션이 맡는다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveMontage;
	// ActiveMontage 가 맡은 방향.
	FVector2D ActiveDir = FVector2D::ZeroVector;
	// 아직 갈아끼우지 않은 후보 몽타주와 그 시각. 일정 시간 유지되면 건다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> PendingMontage;
	double PendingSince = 0.0;
	// 지금 몽타주 배속. 한 틱 이동 상한이 이것을 따라간다.
	float PlayRate = 1.0f;

	// 직전 틱에 놓은 자리. 루트모션 이동량을 이것과의 차이로 잰다.
	FVector LastPlacedLocation = FVector::ZeroVector;
	bool bPlacedOnce = false;
	// 말단별 보정량(벽 법선 방향 cm, 바깥 +). 잰 오차만큼 누적해 고친다.
	float CorrHandL = 0.0f;
	float CorrHandR = 0.0f;
	float CorrFootL = 0.0f;
	float CorrFootR = 0.0f;
	// 보간된 IK 세기. 매 틱 IKTargets 로 옮겨 적는다.
	float WeightHandL = 0.0f;
	float WeightHandR = 0.0f;
	float WeightFootL = 0.0f;
	float WeightFootR = 0.0f;
	FVector EnterLocation = FVector::ZeroVector;
	// 붙잡는 순간의 회전. 위치와 함께 블렌드한다.
	FQuat EnterRotation = FQuat::Identity;
	double EnterTime = 0.0;
	FVBClimbDiagnostics Diag;

	// 밖이 읽는 칸(입력, 축, IK, 배속)을 비우고 몽타주를 멈춘다. 나머지는 다음 시작까지 남는다.
	void ClearOnExit(UAnimInstance* Anim, float StopBlend);
};

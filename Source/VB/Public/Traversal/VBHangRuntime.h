// Copyright VB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VB/VB.h"
#include "Traversal/VBTraversalTypes.h"
#include "VBHangRuntime.generated.h"

class UAnimInstance;
class UAnimMontage;
class USplineComponent;

// 매달림 1초 간격 진단(루트모션 없음과 입력 없음을 가른다). 게임 동작에는 쓰지 않는다.
struct FVBHangDiagnostics
{
	FVBLogGate TraceLog{1.0};
	float DistanceAtTrace = 0.0f;
};

// 매달림 한 번보다 오래 사는 진단. 시작이 비우지 않는다.
struct FVBHangLongLivedDiagnostics
{
	bool bShimmyFallbackWarned = false;
};

// 매달림 한 번의 실행 상태. FVBHangMode::Start 가 통째로 비운다.
USTRUCT()
struct VB_API FVBHangRuntime
{
	GENERATED_BODY()

	TWeakObjectPtr<USplineComponent> Ledge;
	// 스플라인 거리. 유일한 위치 상태이고 캡슐 트랜스폼은 여기서 파생된다.
	float Distance = 0.0f;
	float MinDistance = 0.0f;
	float MaxDistance = 0.0f;
	// X 좌우, Y 앞뒤. 서버는 X 만 받고 앞/뒤 나가기는 따로 요청한다.
	FVector2D MoveInput = FVector2D::ZeroVector;
	float MoveAxis = 0.0f;

	// 지금 도는 옆걸음 몽타주와 재생 위치(초). 위치는 몸통 흔들림 산출에만 쓴다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ShimmyMontage;
	float ShimmyPosition = 0.0f;
	// 지금 위상의 클립 root 흔들림(벽 쪽 +, 위 +). 배치에 더해야 흔들림이 손으로 옮겨가지 않는다.
	float SwayTowardWall = 0.0f;
	float SwayRise = 0.0f;

	// 직전 틱에 놓은 캡슐 위치. 실제 위치와의 차이가 루트모션이 옮긴 옆걸음 진행량이다.
	FVector LastPlacedLocation = FVector::ZeroVector;
	bool bPlacedOnce = false;

	// 대기, 옆걸음 자세 사이를 보간하는 기준 깊이, 높이(cm). 상태 전환 단차만 녹인다.
	float DepthBase = 0.0f;
	float BelowBase = 0.0f;
	// 실제로 쓰는 깊이, 높이(기준 + 흔들림). 흔들림은 보간하지 않는다.
	float DepthCurrent = 0.0f;
	float BelowCurrent = 0.0f;

	// 직전 프레임에 그 손이 뻗는 중이었나. 뻗는 손에는 밀어내기를 안 건다.
	bool bHandReachingL = false;
	bool bHandReachingR = false;
	// 같은 홀드로 나가기를 두 번 보내지 않게.
	bool bExitRequested = false;
	// 축이 한 번 중립으로 돌아와야 앞/뒤 나가기가 무장된다(잡자마자 올라서지 않게).
	bool bExitInputArmed = false;
	// 잡는 순간의 캡슐 위치와 시각. 이 자리에서 매달림 자리로 미끄러진다.
	FVector GrabStartLocation = FVector::ZeroVector;
	double GrabStartTime = 0.0;

	// 매달림 IK 목표와 세기.
	FVBHangIKTargets IKTargets;
	// IK 블렌드 정도 0~1. 잡는 순간 손이 순간이동하지 않게 천천히 올린다.
	float IKAlpha = 0.0f;
	FVBHangDiagnostics Diag;

	// 밖이 읽는 칸(렛지, 입력, 축, 나가기, IK)을 비우고 옆걸음 몽타주를 멈춘다. 나머지는 다음 시작까지 남는다.
	void ClearOnExit(UAnimInstance* Anim, float StopBlend);
};

// Copyright VB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VB/VB.h"
#include "VBLadderRuntime.generated.h"

class AVBLadder;
class UAnimInstance;
class UAnimMontage;
class UPrimitiveComponent;

// A 단계(UpdateIntent)가 정해 같은 프레임 B 단계(UpdatePlacement)가 쓰는 값.
struct FVBLadderFrame
{
	uint64 Frame = 0; // A 가 이 값을 쓴 GFrameCounter
	bool bDrives = false; // 이 머신이 축 위를 움직이나(서버 또는 소유 클라)
	float Want = 0.0f; // 막힌 방향을 버린 위/아래 입력(-1..1)
	float PlayRate = 1.0f; // 이번 프레임 몽타주 배속
	float Top = 0.0f; // 손이 가로대에 닿는 가장 높은 축 높이(cm)
	bool bRootMotionAtIntent = false; // A 시점에 루트모션 재생 중이었나(B 와 다르면 몽타주가 바뀐 것)
};

// 진단 로그 간격 시계와 튐 계측. 게임 동작에는 쓰지 않는다.
struct FVBLadderDiagnostics
{
	FVBLogGate StatusLog{1.0}; // 상태 줄
	FVBLogGate ClampLog{1.0}; // 한 틱 이동을 잘랐다는 줄
	FVBLogGate JumpLog{0.05}; // 보이는 자리가 한 틱에 뛴 줄
	FVBLogGate OrderLog{1.0}; // A, B 단계 어긋남 경고
	FVBLogGate TopOutLog{0.5}; // 꼭대기 올라서기 거절 사유
	// 직전 틱에 캡슐을 놓은 자리. 튐 계측에만 쓴다.
	FVector LastPlacedLocation = FVector::ZeroVector;
	bool bPlacedOnce = false; // 첫 틱은 비교할 직전 위치가 없다
};

// 꼭대기 올라서기 IK 지지 요청. StartTopOut 이 걸고 RecoverFromTraversal 이 내린다.
struct FVBLadderTopOutSupport
{
	TWeakObjectPtr<AVBLadder> Ladder;
	bool bRequested = false;
};

// 사다리 한 번의 탑승 상태. FVBLadderMode::Start 가 통째로 비운다.
USTRUCT()
struct VB_API FVBLadderRuntime
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AVBLadder> ActiveLadder;
	// 축 위 높이(cm, 0=바닥). 유일한 위치 상태이고 캡슐 자리는 여기서 파생된다.
	float HeightOnAxis = 0.0f;
	float MoveInput = 0.0f;
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveMontage;
	FVector EnterLocation = FVector::ZeroVector;
	FQuat EnterRotation = FQuat::Identity;
	double EnterTime = 0.0;
	// 사다리 몽타주 재생 위상(초). 이동은 이 값의 프레임 간 구간에서 나온다(액터 이동량은 밀려난 양까지 섞인다).
	float MontagePosition = 0.0f;
	bool bMontageTracked = false;
	// 멈출 때 가장 가까운 가로대로 당겨 앉는 목표 높이와 진행 여부.
	float SettleTarget = 0.0f;
	bool bSettling = false;
	// 입력이 끊긴 뒤 흐른 시간(초). 문턱을 넘어야 당겨 앉기가 시작된다(탭과 싸우지 않게).
	float IdleQuietTime = 0.0f;
	// 직전 방향 몽타주와 위상. 같은 것을 다시 걸 때 여기서 이어 붙인다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ResumeMontage;
	float ResumePhase = 0.0f;
	FVBLadderFrame Frame;
	// 이 모드가 캡슐 이동 무시에 넣은 막는 상자. 정리 때 넣은 것만 뺀다.
	TWeakObjectPtr<UPrimitiveComponent> IgnoredBlocker;
	FVBLadderTopOutSupport TopOutSupport;
	FVBLadderDiagnostics Diag;

	// 탑승 칸을 비우고 사다리 몽타주를 멈춘다. IgnoredBlocker, TopOutSupport 는 복구까지 남는다.
	void ClearOnExit(UAnimInstance* Anim, float StopBlend);
};

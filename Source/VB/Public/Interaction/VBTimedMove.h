// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBTimedMove.generated.h"

// 서버 시각으로 정해지는 움직임 한 번(알파 From -> To). 모든 기계가 같은 시각에 같은 알파를 계산한다 - 승강기 발판, 물리 문의 문짝.
//   멈춤(bStopped)이면 FromAlpha 에 서 있다. 막힘 판정, 다시 출발, 저장 정책은 쓰는 액터가 각자 가진다.
USTRUCT()
struct VB_API FVBTimedMove
{
	GENERATED_BODY()

	UPROPERTY()
	float FromAlpha = 0.0f;

	UPROPERTY()
	float ToAlpha = 0.0f;

	UPROPERTY()
	double ServerStartTime = 0.0;

	UPROPERTY()
	float Duration = 0.0f;

	UPROPERTY()
	bool bStopped = false;
};

namespace VBTimedMove
{
	// 그 시각의 알파. 양 끝에서 부드럽게 서고 출발한다(smoothstep). 끝에서는 ToAlpha 를 그대로 낸다.
	VB_API float EvaluateAlpha(const FVBTimedMove& Move, double ServerNow);

	// 알파 From -> To 를 Speed 로 가는 시간. 거리나 속도가 0 이하면 0.
	VB_API float TripDuration(float FromAlpha, float ToAlpha, float TravelDistance, float Speed);

	// 모든 기계가 같은 값을 읽는 서버 시각(GameState 의 복제 시각, 없으면 월드 시각).
	VB_API double ServerNow(const UObject* WorldContextObject);
}

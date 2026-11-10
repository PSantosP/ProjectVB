// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBTimedMove.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

float VBTimedMove::EvaluateAlpha(const FVBTimedMove& Move, const double ServerNow)
{
	if (Move.bStopped)
	{
		return Move.FromAlpha;
	}

	if (Move.Duration <= 0.0f)
	{
		return Move.ToAlpha;
	}

	const float T = static_cast<float>((ServerNow - Move.ServerStartTime) / Move.Duration);
	// 끝에서는 목표값을 그대로 낸다 - 도착(목표와 같을 때만 난다)이 Lerp 의 반올림에 기대지 않게.
	//   두 끝(0, 1)에서는 Lerp 도 정확히 끝나 시험으로 이 분기를 가를 수 없다(float32 0.001 간격 전수 확인).
	if (T >= 1.0f)
	{
		return Move.ToAlpha;
	}

	return FMath::Lerp(Move.FromAlpha, Move.ToAlpha, FMath::SmoothStep(0.0f, 1.0f, FMath::Max(T, 0.0f)));
}

float VBTimedMove::TripDuration(const float FromAlpha, const float ToAlpha, const float TravelDistance, const float Speed)
{
	if (Speed <= 0.0f || TravelDistance <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Abs(ToAlpha - FromAlpha) * TravelDistance / Speed;
}

double VBTimedMove::ServerNow(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return 0.0;
	}

	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

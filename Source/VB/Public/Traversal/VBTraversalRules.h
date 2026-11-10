// Copyright VB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


// 트래버설의 순수 값 판정. 월드를 묻지 않고 받은 값만 본다(테스트가 직접 부른다).
// 꼭대기 올라서기 자리 찾기의 결과 - 찾았으면 렛지 점, 못 찾았으면 거절 이유(로그용). 사다리, 등반이 같이 쓴다.
struct FVBTopOutResult
{
	bool bFound = false;
	FVector LedgePoint = FVector::ZeroVector;
	FString Refusal;
};

namespace VBTraversalRules
{
	namespace PlayRate
	{
		// 이동 몽타주 배속. 질주 중 그 방향을 밀 때만 데이터 배율, 아니면 1.
		//   이동은 루트모션이라 배속만 바꾸면 이동도 같이 빨라진다(루트모션에 또 곱하지 않는다).
		VB_API float ResolveTraversalPlayRate(bool bFast, float WantInput, float FastUpRate, float FastDownRate);
	}

	namespace Auto
	{
		// 질주 자동 진입 의도. 질주 중이고 입력이 캐릭터 앞쪽이며 속도가 충분하면 true.
		VB_API bool HasAutoTraversalIntent(bool bSprinting, float InputForwardDot, float Speed2D, float MinForwardDot,
			float MinSpeed);
		// 놓은 뒤에도 아직 질주+이동 키를 누르고 있나. 매달림, 사다리 중엔 가속으로 못 봐서 원래 입력을 본다.
		VB_API bool HoldsAutoTraversalInput(bool bSprinting, const FVector2D& RawMoveInput);
	}

	namespace Ladder
	{
		// 사다리에 붙을 자리인가(축에서 MaxDistance 안, 타는 면 쪽). 클라 찾기와 서버 재검사가 같이 쓴다.
		VB_API bool IsLadderStartPlausible(const FVector& RequesterLocation, const FVector& AxisNear,
			const FVector& FacingNormal, float MaxDistance);
		// 꼭대기 올라서기를 찾아볼 때인가(서버, 붙는 보간 끝, 위 입력, 꼭대기 한계 도달).
		//   보간 중에는 캐릭터가 올라서기 시작 자세와 맞지 않아 제외한다.
		VB_API bool ShouldTryLadderTopOut(bool bAuthority, bool bEntryAligned, float WantInput, float HeightOnAxis,
			float TopHeight, float MinUpInput);
		// 꼭대기 위 바닥을 찾는 높이 구간. 꼭대기보다 낮은 바닥은 막는 상자에 걸쳐 복구되므로 받지 않는다.
		VB_API FFloatInterval ComputeLadderTopOutBand(float TopZ, float Skin, float Tolerance);
	}

	namespace Climb
	{
		// 등반 꼭대기에 설 바닥인가. 걸을 수 있는 면이 Band 안이고 내려 앉힌 캡슐이 그 바닥에서 StepTolerance 안에 닿아야 한다.
		VB_API bool IsClimbTopOutFloorUsable(float CenterFloorNormalZ, float WalkableFloorZ, float CenterFloorZ,
			float CapsuleContactZ,
			const FFloatInterval& Band, float StepTolerance);
	}
}

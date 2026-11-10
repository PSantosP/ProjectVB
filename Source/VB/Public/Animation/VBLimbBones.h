// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 손발 뼈 이름. 스켈레톤의 이름이라 조정값이 아니다.
namespace VBLimbBones
{
	inline const FName HandL(TEXT("hand_l"));
	inline const FName HandR(TEXT("hand_r"));
	inline const FName FootL(TEXT("foot_l"));
	inline const FName FootR(TEXT("foot_r"));
	inline const FName BallL(TEXT("ball_l"));
	inline const FName BallR(TEXT("ball_r"));

	// 네 말단. 같은 칸은 같은 말단이다: IK 가 옮기는 끝 뼈와 벽, 가로대에 닿는 뼈.
	inline const FName Ends[4] = { HandL, HandR, FootL, FootR };
	inline const FName Contacts[4] = { HandL, HandR, BallL, BallR };
}

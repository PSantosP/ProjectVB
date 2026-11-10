// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ACharacter;
class UAnimInstance;
class UAnimMontage;
class UVBClimbAnimSet;

// 트래버설 모드(매달림 옆걸음, 등반, 사다리)가 같이 쓰는 몽타주 재생 함수. 상태는 없고 각 모드가 재생 기록을 가진다.
namespace VBTraversalMontage
{
	// 캐릭터 메시의 애님 인스턴스. 캐릭터나 메시가 없으면 null.
	UAnimInstance* FindAnim(const ACharacter* Character);

	// 몽타주 블렌드아웃 시간. 복구는 클립 묶음 없이도 돌아서 없으면 클래스 기본값을 준다.
	float StopBlendTime(const UVBClimbAnimSet* Clips);

	// Wanted 를 첫 섹션 반복으로 재생하고 재생 중인 몽타주를 반환한다(null 이면 Active 정지). 새로 시작하면 bOutStarted = true.
	UAnimMontage* DriveLoopingMontage(UAnimInstance* Anim, UAnimMontage* Wanted, UAnimMontage* Active, float StopBlend,
		bool& bOutStarted, float StartTime = 0.0f);
}

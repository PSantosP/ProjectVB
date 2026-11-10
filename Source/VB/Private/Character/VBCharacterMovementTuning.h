// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AVBCharacter;
class UCharacterMovementComponent;
class UVBMovementConfig;
class UVBWeaponStateComponent;

// 이동 설정 자산의 값을 이동 컴포넌트에 넣는 함수 모음. 캐릭터가 게임 스레드에서 부른다.
namespace VBCharacterMovementTuning
{
	// CMC 물리 기본값을 설정 자산에서 적용한다(생성자에는 BP 배선이 아직 없어 BeginPlay 에서 부른다).
	void ApplyConfig(UCharacterMovementComponent& CMC, const UVBMovementConfig& Config);

	// 이동, 포즈 소유 경계를 지금 상태로 확인해 회전율을 고른다. 이동 컴포넌트가 회전 직전에만 부른다.
	void RefreshRotationRate(const AVBCharacter& Body, UCharacterMovementComponent& CMC, const UVBWeaponStateComponent* WeaponState);
}

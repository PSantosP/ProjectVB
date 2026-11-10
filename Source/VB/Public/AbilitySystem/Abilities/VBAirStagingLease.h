// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UVBAirAfterimageComponent;
class UVBCharacterAppearanceComponent;
class UVBTargetLockComponent;

// 공중 콤보가 빌린 연출 셋(잔상, 몸 페이드, 카메라 구도). 빌린 곳과 요청 번호를 기억해 한 번에 돌려준다.
// 요청 번호 0 은 거절이라 기억하지 않는다.
struct VB_API FVBAirStagingLease
{
	void HoldAfterimage(UVBAirAfterimageComponent& InAfterimage, uint64 RequestId);
	void HoldBodyFade(UVBCharacterAppearanceComponent& Appearance, uint64 RequestId);
	void HoldCameraFraming(UVBTargetLockComponent& Lock, uint64 RequestId);

	// 빌린 것을 전부 돌려준다. 다시 불러도 안전하다.
	void Release();

private:
	TWeakObjectPtr<UVBAirAfterimageComponent> Afterimage;
	uint64 AfterimageRequestId = 0;
	TWeakObjectPtr<UVBCharacterAppearanceComponent> FadeAppearance;
	uint64 BodyFadeRequestId = 0;
	TWeakObjectPtr<UVBTargetLockComponent> CameraFramingOwner;
	uint64 CameraFramingRequestId = 0;
};

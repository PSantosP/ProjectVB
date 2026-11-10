#pragma once
#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/VBEnvironmentCameraSolver.h"
#include "VBPlayerCameraManager.generated.h"

class UVBEnvironmentCameraComponent;

/** GameplayCameras 와 modifier 뒤, view-target 블렌드 전에 Pawn 자기 시점에 환경 안전 보정을 적용한다. */
UCLASS()
class VB_API AVBPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
public:
	// 모든 흔들림(타격, 패링 대체, 문맥 효과, 애님 노티, 엔진 ClientStartCameraShake 포함)이 지나는 입구 - 사용자 흔들림 배율을 곱한다.
	using APlayerCameraManager::StartCameraShake;
	virtual UCameraShakeBase* StartCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, const FAddCameraShakeParams& Params) override;
protected:
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;
private:
	FVBEnvironmentCameraSolver EnvironmentSolver;
	TWeakObjectPtr<UVBEnvironmentCameraComponent> LastComponent;
	// 직전 프레임의 UVBEnvironmentCameraComponent::OwnsCollision() 값 - 맡다가 놓친 순간에 Warning 한 줄.
	bool bOwnedCollision = false;
};

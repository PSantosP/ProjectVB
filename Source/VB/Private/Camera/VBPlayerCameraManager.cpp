#include "Camera/VBPlayerCameraManager.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBPlayerCamera, Log, All);

UCameraShakeBase* AVBPlayerCameraManager::StartCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, const FAddCameraShakeParams& Params)
{
	const float UserScale = UVBSaveGameSubsystem::GetSettingsData(this).CameraShakeScale;
	if (UserScale <= 0.0f)
	{
		return nullptr;
	}

	FAddCameraShakeParams Scaled = Params;
	Scaled.Scale *= UserScale;
	return Super::StartCameraShake(ShakeClass, Scaled);
}

void AVBPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	Super::UpdateViewTarget(OutVT, DeltaTime);
	auto* Character = PCOwner ? Cast<ACharacter>(PCOwner->GetPawn()) : nullptr;
	// 전환의 다른 쪽(처형 카메라 등)은 건드리지 않는다 - 보정은 캐릭터 시점에만 걸고, 전환은 보정된 시점끼리 섞는다(Lyra).
	if (!Character || OutVT.Target != Character)
	{
		return;
	}
	// 엔진이 잠근 나가는 시점은 이번 프레임에 다시 계산하지 않았다 - 이미 보정된 값이다.
	if (PendingViewTarget.Target && BlendParams.bLockOutgoing && OutVT.Equal(ViewTarget))
	{
		return;
	}

	auto* Component = UVBEnvironmentCameraComponent::FindFor(Character);
	const bool bOwns = Component && Component->OwnsCollision();
	if (bOwnedCollision && !bOwns)
	{
		UE_LOG(LogVBPlayerCamera, Warning,
			TEXT("%s: environment camera lost collision ownership - no camera collision until the director activates again"),
			*Character->GetName());
	}

	bOwnedCollision = bOwns;
	if (!PCOwner->IsLocalController() || !bOwns || !Component->Config || (CameraStyle != NAME_Default && CameraStyle != NAME_None))
	{
		EnvironmentSolver.Reset();
		LastComponent.Reset();
		return;
	}

	if (LastComponent.Get() != Component)
	{
		EnvironmentSolver.Reset();
	}

	LastComponent = Component;
	EnvironmentSolver.Update(Character, Component->Config, DeltaTime, bGameCameraCutThisFrame, OutVT.POV);
	SetActorLocationAndRotation(OutVT.POV.Location, OutVT.POV.Rotation, false);
}

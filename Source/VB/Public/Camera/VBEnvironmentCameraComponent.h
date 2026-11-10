#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBEnvironmentCameraComponent.generated.h"

class UBlueprintCameraDirectorEvaluator;
class UCameraRigAsset;
class UGameplayCameraComponentBase;
class UVBEnvironmentCameraConfig;
class UCameraAsset;

/** Director 활성화 때 로컬 충돌 처리의 소유자를 고른다. Actor Tick 에서는 쓰지 않는다. */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBEnvironmentCameraComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBEnvironmentCameraComponent();
	// 그 액터에 붙은 것(BP_VBCharacter 가 붙인다). 없으면 nullptr.
	static UVBEnvironmentCameraComponent* FindFor(const AActor* Actor);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Camera")
	TObjectPtr<UVBEnvironmentCameraConfig> Config;

	/** Director 의 충돌 활성화만 대신한다. 카메라를 소유한 액터에서 이 컴포넌트를 찾고(FindFor),
	 *  없거나 Prepare 가 실패하면 FallbackRig 를 대신 켠다. */
	UFUNCTION(BlueprintCallable, Category="VB|Camera")
	static void ActivateEnvironmentCollision(UObject* EvaluationContextOwner,
		UBlueprintCameraDirectorEvaluator* Director, UCameraRigAsset* FallbackRig);

	void ReleaseCollisionOwnership();
	bool OwnsCollision() const;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	bool Prepare(UGameplayCameraComponentBase* Camera);
	TWeakObjectPtr<UGameplayCameraComponentBase> ActiveCamera;
	TWeakObjectPtr<UCameraAsset> ActiveCameraAsset;
};

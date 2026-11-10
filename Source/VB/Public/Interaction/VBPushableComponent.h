#pragma once

#include "Interaction/VBInteractableComponent.h"
#include "CollisionQueryParams.h"
#include "VBPushableComponent.generated.h"

class ACharacter;
class UVBPushableConfig;
class UAnimInstance;

UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBPushableComponent : public UVBInteractableComponent
{
	GENERATED_BODY()
public:
	UVBPushableComponent();
	const UVBPushableConfig* GetPushConfig() const;
	FTransform GetStandTransform() const;
	bool Reserve(ACharacter* Character);
	bool StartPushing(ACharacter* Character);
	void StopPushing(ACharacter* Character);
	void SetPushInput(ACharacter* Character, float Value);
	void RequestRelease(ACharacter* Character);
	bool GetHandTargets(UAnimInstance* Anim, FTransform& Left, FTransform& Right, float& Weight) const;
	// 밀기 이동 스윕이 무시할 것 - 대상 자신, 미는 캐릭터, 같은 대상의 물리 조각(TargetParts).
	// 조각을 빼면 처음부터 겹쳐 있는 자기 부품에 막혀 한 칸도 못 움직인다.
	static FCollisionQueryParams MakePushSweepParams(const AActor* Owner, const AActor* Character, TConstArrayView<TObjectPtr<AActor>> Parts);

	//~ 자리 저장 - 민 자리가 세이브에 남는다. 기준 = 처음 놓인 자리(InitialLocation).
	virtual bool IsTransformSaved() const override { return bSaveTransform; }
	virtual bool CaptureMovedTransform(FTransform& Out) const override;
	virtual bool ApplySavedTransform(const FTransform& Saved) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	// 민 자리를 세이브에 남기는가. 켜짐 저장(bSaveState, 밀기 물체는 기본 끔)과 따로다 - 퍼즐 상자는 민 자리가 곧 진행이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Save")
	bool bSaveTransform = true;

private:
	TWeakObjectPtr<ACharacter> Pusher;
	FVector InitialLocation = FVector::ZeroVector;
	float Input = 0.0f;
	double LastInputTime = 0.0;
	uint8 PreviousMovementMode = 0;
	bool bMoving = false;
	bool bReleasing = false;
};

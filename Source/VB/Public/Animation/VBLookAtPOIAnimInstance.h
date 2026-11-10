#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VBLookAtPOIAnimInstance.generated.h"

class UDataAsset;
class FArrayProperty;

/** Linked input pose 를 원본 GASP Control Rig 에 전달한다. 샘플 애니메이션이나 BP 판정 그래프는 없다. */
UCLASS(Transient, Blueprintable)
class VB_API UVBLookAtPOIAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look At") TObjectPtr<UDataAsset> LookAtSettings;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Look At") FVector LookAtTarget = FVector::ZeroVector;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Look At") bool bLookAtEnabled = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Look At") bool bInteractionAttention = false;
	/** Wildcard 배열로 원본 rig 의 사용자 정의 struct 입력 계약을 그대로 지킨다. */
	UFUNCTION(BlueprintPure, CustomThunk, Category="Look At", meta=(ArrayParm="Values", BlueprintThreadSafe))
	void GetLookAtWeightOverrides(TArray<int32>& Values) const;
	DECLARE_FUNCTION(execGetLookAtWeightOverrides);
	UFUNCTION(BlueprintPure, CustomThunk, Category="Look At", meta=(ArrayParm="Values", BlueprintThreadSafe))
	void GetLookAtLimitOverrides(TArray<int32>& Values) const;
	DECLARE_FUNCTION(execGetLookAtLimitOverrides);
private:
	UPROPERTY(Transient) TArray<FName> BodyPartLabels;
	void BuildCurveOverrides(FArrayProperty* ArrayProperty, void* ArrayAddress, bool bLimits) const;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBLookAtPOIComponent.generated.h"

/** 원본 GASP CR_AdjustToPOI rig 가 볼 대상을 game thread 에서 고른다 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VB_API UVBLookAtPOIComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBLookAtPOIComponent();
	UFUNCTION(BlueprintCallable, Category="VB|Look At") void SetLookAtActor(AActor* Target);
	UFUNCTION(BlueprintCallable, Category="VB|Look At") void SetLookAtLocation(FVector WorldLocation);
	UFUNCTION(BlueprintCallable, Category="VB|Look At") void ClearLookAtTarget();
	/** 우선순위는 명시 > 대화 > lock-on > 플레이어 주의 > AI focus. game thread 전용 */
	virtual bool ResolveTarget(FVector& OutWorldLocation) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bUseLockedTarget = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bUseAIFocus = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") FName TargetSocket = TEXT("head");
private:
	UPROPERTY(Transient) TWeakObjectPtr<AActor> TargetActor;
	FVector TargetLocation = FVector::ZeroVector;
	bool bHasLocation = false;
};

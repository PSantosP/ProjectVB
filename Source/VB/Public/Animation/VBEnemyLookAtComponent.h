#pragma once

#include "CoreMinimal.h"
#include "Animation/VBLookAtPOIComponent.h"
#include "VBEnemyLookAtComponent.generated.h"

/** AI 의 시선 대상을 복제한다 - 전투, 카메라 판단은 바꾸지 않는다 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VB_API UVBEnemyLookAtComponent : public UVBLookAtPOIComponent
{
	GENERATED_BODY()
public:
	UVBEnemyLookAtComponent();
	virtual bool ResolveTarget(FVector& OutWorldLocation) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
private:
	void UpdateFocusActor();
	UFUNCTION() void OnRep_FocusActor();
	UPROPERTY(ReplicatedUsing=OnRep_FocusActor) TObjectPtr<AActor> FocusActor;
};

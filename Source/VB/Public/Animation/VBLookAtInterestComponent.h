#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBLookAtInterestComponent.generated.h"

class UVBLookAtInterestComponent;

/** 월드별 weak 등록부 - 스트리밍 아웃된 관심 대상이 자기 액터를 붙잡지 않게 한다. */
UCLASS()
class VB_API UVBLookAtInterestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	void Register(UVBLookAtInterestComponent* Interest);
	void Unregister(UVBLookAtInterestComponent* Interest);
	const TArray<TWeakObjectPtr<UVBLookAtInterestComponent>>& GetInterests();
private:
	TArray<TWeakObjectPtr<UVBLookAtInterestComponent>> Interests;
};

/** 붙인 액터만 시선 관심 대상이 된다. 위치는 이 컴포넌트이거나 소유 캐릭터의 소켓이다. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VB_API UVBLookAtInterestComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bInterestEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") FName TargetSocket = NAME_None;
	FVector GetInterestLocation() const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

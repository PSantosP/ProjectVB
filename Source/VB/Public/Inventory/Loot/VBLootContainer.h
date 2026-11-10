// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/VBToggleProp.h"
#include "VBLootContainer.generated.h"

class UVBLootTable;

/**
 * 내용물이 든 여닫는 소품(서랍장, 상자). 처음 열 때 Table 을 한 번 굴리고, 못 받은 나머지는 다시 열면 받는다.
 * 남은 몫은 UVBLootContainerSubsystem 이 관리한다. 일회용이면 다 털린 뒤에만 잠긴다.
 * 완료 효과에 보상표 주기를 같이 달지 않는다 - 두 번 준다.
 */
UCLASS()
class VB_API AVBLootContainer : public AVBToggleProp
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool IsOneShotSpent() const override;

	// 이 서랍장에 무엇이 들었나. 놓인 자리마다 다르다. 비면 열어도 아무것도 없다(다 털린 것으로 적힌다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Loot")
	TObjectPtr<const UVBLootTable> Table;

private:
	UFUNCTION()
	void HandleCompletionApplied(AActor* InteractingActor);

	void HandleContentsLoaded();

	// 남은 몫 기록의 키. 상호작용 저장 키와 같고, 저장을 끈 대상이면 액터 이름이다.
	FName GetContentsKey() const;

	FDelegateHandle ContentsLoadedHandle;
};

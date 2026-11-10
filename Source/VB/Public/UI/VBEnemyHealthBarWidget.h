// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBAttributeBarWidget.h"
#include "VBEnemyHealthBarWidget.generated.h"

class AActor;
class UTextBlock;

/** 부모가 지정한 적의 체력과 자세 게이지를 표시한다. */
UCLASS()
class VB_API UVBEnemyHealthBarWidget : public UVBAttributeBarWidget
{
	GENERATED_BODY()

public:
	// 그 적의 ASC 에 묶고 이름(UVBEnemyConfig::DisplayName)을 쓴다. ASC 가 없으면 풀고 false - 부모가 숨긴다.
	bool ShowEnemy(AActor* Enemy);
	void Clear();

protected:
	virtual void OnASCBound(UAbilitySystemComponent& ASC) override;
	virtual void OnUnbound() override;
	virtual void OnHealthRefreshed(float Percent) override;

	// 적 이름. 없어도 바는 동작한다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtName;

	// 자세가 무너질수록 차며, 처형 창 동안 가득 찬 상태를 유지한다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UProgressBar> PostureBar;

private:
	void RefreshPosture();
	void HandlePostureChanged(const FOnAttributeChangeData& Data);
	void HandleWindowCueChanged(FGameplayTag Tag, int32 Count);

	FVBAttributeSubscription PoiseChanged;
	FVBAttributeSubscription MaxPoiseChanged;
	TWeakObjectPtr<UAbilitySystemComponent> CueSource;
	FDelegateHandle WindowCueChangedHandle;
};

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBComponentSubscription.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBProgressionLineWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UVBProgressionComponent;

/**
 * HUD 의 레벨, 경험치 줄과 막대(C++ 로 짓는다). 그리기만 한다.
 * 구성 때 PlayerState 의 성장에 묶고 해체 때 푼다. 묶기 전에는 접어 둔다.
 */
UCLASS()
class VB_API UVBProgressionLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UVBProgressionLineWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 현지화 키는 VBHUD 키 그대로다(번역이 키로 붙는다).
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD", meta=(ToolTip="{0}=레벨, {1}=이번 레벨에서 모은 경험치, {2}=다음 레벨까지 필요한 경험치"))
	FText LineFormat;

	UPROPERTY(EditDefaultsOnly, Category="VB|HUD", meta=(ToolTip="최고 레벨일 때. {0}=레벨"))
	FText MaxFormat;

	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	FVector2D BarSize = FVector2D(260.0f, 6.0f);

private:
	void BindProgression(UVBProgressionComponent* Component);
	void Refresh();

	UPROPERTY()
	TObjectPtr<UTextBlock> LineText;
	UPROPERTY()
	TObjectPtr<UProgressBar> LineBar;

	FVBPlayerStateBinding PlayerStateSet;
	TVBComponentSubscription<UVBProgressionComponent> Progression;
};

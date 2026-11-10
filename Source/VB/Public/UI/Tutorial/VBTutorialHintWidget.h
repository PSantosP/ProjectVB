// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBTutorialHintWidget.generated.h"

class UVBTutorialComponent;
class UVBUIManagerSubsystem;
class UImage;
class UTextBlock;

/**
 * HUD 의 튜토리얼 힌트. 튜토리얼 컴포넌트의 지금 힌트 단계를 그리기만 한다.
 * 게임 입력이 막히면(메뉴, 사망, 시네마틱) 감춘다.
 */
UCLASS(Abstract)
class VB_API UVBTutorialHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UVBTutorialHintWidget(const FObjectInitializer& ObjectInitializer);
	// 구독을 옮기고 즉시 그린다. 소유자가 AVBPlayerController 면 구성 때 스스로 부른다.
	void BindTo(UVBTutorialComponent* Component);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TxtTitle;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TxtBody;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TxtKey;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> ImgKey; // 키 그림 칸. 없으면 TxtKey 글자만

private:
	void Unbind();
	void RefreshSnapshot();
	void RefreshPresentation();
	bool TickPresentation(float DeltaSeconds);
	FVBPlayerStateBinding PlayerStateSet;
	TWeakObjectPtr<UVBTutorialComponent> Tutorial;
	TWeakObjectPtr<UVBUIManagerSubsystem> UIManager;
	FDelegateHandle ChangedHandle;
	FDelegateHandle InputAvailabilityHandle;
	FTSTicker::FDelegateHandle PresentationHandle;
	// 보이는 단계의 복사본. 객체를 가리킬 수 있어 GC 가 보게 UPROPERTY 다.
	UPROPERTY(Transient)
	FVBTutorialStep ShownStep;
	bool bHasHint = false;
};

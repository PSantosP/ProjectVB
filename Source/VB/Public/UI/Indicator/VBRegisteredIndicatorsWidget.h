// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Indicator/VBIndicatorTypes.h"
#include "UI/VBWorldIndicatorWidget.h"
#include "VBRegisteredIndicatorsWidget.generated.h"

class UCanvasPanel;

// 표시기 목록(UVBIndicatorRegistrySubsystem)의 요청 하나를 그리는 위젯의 부모. 요청의 Data 가 이 클래스(의 자식)여야 그려진다.
UCLASS(Abstract)
class VB_API UVBRegisteredIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()
};

/**
 * 표시기 층의 한 갈래: 표시기 목록에서 이 보는 사람의 요청을 읽어 요청마다 위젯 하나를 그 요청의 클래스로 만들고, 앵커 위에 띄운다.
 * 무엇을 요청할지는 요청하는 쪽(예: UVBExecutePromptComponent), 어떻게 생겼는지는 요청의 위젯 클래스가 정한다.
 */
UCLASS()
class VB_API UVBRegisteredIndicatorsWidget : public UVBWorldIndicatorWidget
{
	GENERATED_BODY()

public:
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot) override;

	// 그린 것을 이 요청들에 맞춘다(투영 없이) - 새 번호는 위젯을 하나 만들고, 사라진 번호의 위젯은 떼고, 있던 번호는 그대로 둔다.
	void SyncRequests(TConstArrayView<TPair<uint64, FVBIndicatorRequest>> Requests);

protected:
	virtual void NativeOnInitialized() override;

	// 위젯의 어느 점이 앵커 자리에 오나. (0.5, 1) = 아래 가운데 - 대상 바로 위에 뜬다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D ChildAlignment = FVector2D(0.5f, 1.0f);

private:
	// 이 갈래의 캔버스(WBP 루트가 캔버스면 그것, 아니면 코드가 만든다).
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	// 요청 번호 -> 그 요청을 그리는 위젯.
	UPROPERTY(Transient)
	TMap<uint64, TObjectPtr<UVBRegisteredIndicatorWidget>> Shown;

	// 요청의 Data 가 그릴 수 있는 위젯 클래스가 아니면 그 클래스마다 한 번 말한다.
	TSet<FObjectKey> ReportedBadData;
};

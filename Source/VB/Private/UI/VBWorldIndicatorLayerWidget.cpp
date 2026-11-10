// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBWorldIndicatorLayerWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "GameFramework/PlayerController.h"
#include "UI/Quest/VBObjectiveMarkerLayerWidget.h"
#include "UI/VBInteractPromptWidget.h"
#include "UI/VBNearbyEnemyBarsWidget.h"
#include "UI/VBOffscreenThreatWidget.h"
#include "UI/Indicator/VBRegisteredIndicatorsWidget.h"
#include "UI/VBTargetHealthIndicatorWidget.h"
#include "UI/VBWorldIndicatorProjection.h"
#include "UI/VBWorldIndicatorWidget.h"
#include "VB/VB.h"

void UVBWorldIndicatorLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bIndicatorsCreated)
	{
		CreateIndicators();
	}
}

void UVBWorldIndicatorLayerWidget::CreateIndicators()
{
	bIndicatorsCreated = true;
	// 붙이는 순서 = 겹치는 순서(같은 ZOrder 면 나중 것이 위). 목표 마커가 맨 아래, 프롬프트가 맨 위 - HUD 에 있을 때와 같다.
	// 처음 보임은 HUD 에 있을 때와 같다: 마커 층은 늘 깔려 있고(입력은 안 받음), 화살표, 프롬프트는 첫 갱신 전까지 숨는다.
	AddIndicator(ObjectiveMarkerLayerClass, TEXT("ObjectiveMarkerLayerClass(목표 마커 층)"), ESlateVisibility::HitTestInvisible,
		[](UCanvasPanelSlot& ChildSlot)
		{
			ChildSlot.SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			ChildSlot.SetOffsets(FMargin(0.0f));
		});
	AddIndicator(OffscreenThreatClass, TEXT("OffscreenThreatClass(화면 밖 위협 화살표)"), ESlateVisibility::Collapsed,
		[this](UCanvasPanelSlot& ChildSlot)
		{
			ChildSlot.SetAlignment(FVector2D(0.5f, 0.5f));
			ChildSlot.SetAutoSize(false);
			ChildSlot.SetSize(OffscreenThreatSize);
		});
	// 근처 적 바는 캔버스 전체에 깔고(바는 그 안에 자기 자리를 잡는다) 락온 대상 바 아래에 둔다.
	AddIndicator(NearbyEnemyBarsClass, TEXT("NearbyEnemyBarsClass(근처 적 체력 바)"), ESlateVisibility::HitTestInvisible,
		[](UCanvasPanelSlot& ChildSlot)
		{
			ChildSlot.SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			ChildSlot.SetOffsets(FMargin(0.0f));
		});
	// 락온 대상 바는 프롬프트 아래에 둔다 - 둘이 겹치면 지금 할 수 있는 행동이 먼저 읽혀야 한다.
	AddIndicator(TargetHealthIndicatorClass, TEXT("TargetHealthIndicatorClass(락온 대상 체력 바)"), ESlateVisibility::Collapsed,
		[this](UCanvasPanelSlot& ChildSlot)
		{
			ChildSlot.SetAlignment(TargetHealthAlignment);
			ChildSlot.SetAutoSize(true);
		});
	// 등록 표시기(처형 키 표시 등)는 캔버스 전체에 깔고 요청마다 자기 자리를 잡는다. 지금 할 수 있는 상호작용이 그 위에 읽힌다.
	AddIndicator(RegisteredIndicatorsClass, TEXT("RegisteredIndicatorsClass(등록 표시기)"), ESlateVisibility::HitTestInvisible,
		[](UCanvasPanelSlot& ChildSlot)
		{
			ChildSlot.SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			ChildSlot.SetOffsets(FMargin(0.0f));
		});
	AddIndicator(InteractPromptClass, TEXT("InteractPromptClass(상호작용 프롬프트)"), ESlateVisibility::Collapsed,
		[this](UCanvasPanelSlot& ChildSlot)
		{
			ChildSlot.SetAlignment(InteractPromptAlignment);
			ChildSlot.SetAutoSize(true);
		});
}

void UVBWorldIndicatorLayerWidget::AddIndicator(TSubclassOf<UVBWorldIndicatorWidget> Class, const TCHAR* What,
	ESlateVisibility InitialVisibility, TFunctionRef<void(UCanvasPanelSlot&)> Arrange)
{
	// 못 붙이면 그 표시만 없다. 조용히 빠지면 안 뜨는 것을 자산 문제로 못 잡으니 로그를 남긴다.
	if (!Class)
	{
		VB_LOG(Warning, "월드 표시기 층: %s 미할당 (WBP_WorldIndicatorLayer 클래스 기본값)", What);
		return;
	}

	if (!IndicatorCanvas)
	{
		VB_LOG(Warning, "월드 표시기 층: IndicatorCanvas 가 없어 %s 를 붙일 수 없다", What);
		return;
	}

	UVBWorldIndicatorWidget* Indicator = CreateWidget<UVBWorldIndicatorWidget>(GetOwningPlayer(), Class);
	UCanvasPanelSlot* IndicatorSlot = Indicator ? IndicatorCanvas->AddChildToCanvas(Indicator) : nullptr;
	if (!IndicatorSlot)
	{
		return;
	}

	Arrange(*IndicatorSlot);
	Indicator->SetVisibility(InitialVisibility);
	Indicators.Add(Indicator);
}

void UVBWorldIndicatorLayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const APlayerController* PC = GetOwningPlayer();
	const FVBIndicatorView View = PC ? VBWorldIndicatorProjection::MakeView(*PC, MyGeometry.GetLocalSize()) : FVBIndicatorView();
	for (UVBWorldIndicatorWidget* Indicator : Indicators)
	{
		// 이 층 캔버스에 붙어 있을 때만 움직인다 - 누가 갈래를 다른 부모로 옮겼으면 그 자리를 건드리지 않는다.
		UCanvasPanelSlot* IndicatorSlot = Indicator ? Cast<UCanvasPanelSlot>(Indicator->Slot) : nullptr;
		if (IndicatorSlot && IndicatorSlot->Parent == IndicatorCanvas)
		{
			Indicator->UpdateIndicator(View, *IndicatorSlot);
		}
	}
}

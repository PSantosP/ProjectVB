// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "UI/Slots/VBHUDLayoutRuntime.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Data/VBHUDLayoutData.h"
#include "GameFramework/PlayerController.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "VB/VB.h"

void VBHUDLayout::Build(UUserWidget& Owner, UCanvasPanel& Canvas, const UVBHUDLayoutData& Layout,
	TArray<TObjectPtr<UVBUISlotWidget>>& Slots, TArray<FVBUISlotHandle>& Handles)
{
	// 저작 실수는 검사기와 같은 문장으로 알린다.
	TArray<FString> Problems;
	Layout.CollectProblems(Problems);
	for (const FString& Problem : Problems)
	{
		VB_LOG(Warning, "HUD: %s 의 %s", *GetNameSafe(&Layout), *Problem);
	}

	// 자리 먼저(행 순서 = 같은 ZOrder 안 그리기 순서), 등록은 그 뒤.
	for (const FVBUISlotPlacement& Placement : Layout.Slots)
	{
		UVBUISlotWidget* SlotWidget = Placement.Slot.IsValid() ? CreateWidget<UVBUISlotWidget>(Owner.GetOwningPlayer(),
			UVBUISlotWidget::StaticClass()) : nullptr;
		if (!SlotWidget)
		{
			continue;
		}
		// 태그, 소유자는 구성 전에 정한다.
		SlotWidget->SetSlot(Placement.Slot, &Owner);
		UCanvasPanelSlot* PlacementSlot = Canvas.AddChildToCanvas(SlotWidget);
		if (!PlacementSlot)
		{
			continue;
		}

		PlacementSlot->SetAnchors(Placement.Anchors);
		PlacementSlot->SetAlignment(Placement.Alignment);
		if (Placement.IsFullScreen())
		{
			PlacementSlot->SetOffsets(FMargin(0.0f));
		}
		else
		{
			PlacementSlot->SetPosition(Placement.Position);
			PlacementSlot->SetSize(Placement.Size);
			PlacementSlot->SetAutoSize(Placement.bAutoSize);
		}

		PlacementSlot->SetZOrder(Placement.ZOrder);
		Slots.Add(SlotWidget);
	}

	UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(Owner.GetOwningPlayer());
	if (!Registry)
	{
		VB_LOG(Warning, "HUD: 주인 컨트롤러에 로컬 플레이어의 UI 슬롯 등록소가 없다 - 배치 데이터의 줄이 비어 있다");
		return;
	}

	for (const FVBUISlotDefaultWidget& Row : Layout.Widgets)
	{
		if (!Row.WidgetClass)
		{
			continue; // 필수면 CollectProblems 가 알렸다. 선택이면 끈 것이다.
		}

		FVBUISlotRequest Request;
		Request.Slot = Row.Slot;
		Request.WidgetClass = Row.WidgetClass;
		Request.Order = Row.Order;
		if (Row.bSetInitialVisibility)
		{
			Request.InitialVisibility = Row.InitialVisibility;
		}
		// 같은 배치 소유자가 만든 자리만 기본 위젯을 받는다.
		Request.Scope = &Owner;
		Request.Registrant = &Owner;
		FVBUISlotHandle Handle = Registry->RegisterWidget(Request);
		if (Handle.IsValid())
		{
			Handles.Add(Handle);
		}
	}
}

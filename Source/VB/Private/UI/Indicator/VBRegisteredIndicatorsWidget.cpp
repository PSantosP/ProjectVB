// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Indicator/VBRegisteredIndicatorsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/Indicator/VBIndicatorRegistrySubsystem.h"
#include "UObject/ObjectKey.h"
#include "VB/VB.h"

void UVBRegisteredIndicatorsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 모양을 뺀 갈래라 WBP 에 캔버스가 없어도 된다 - 없으면 루트로 하나 만든다.
	Canvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!Canvas && WidgetTree && !WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RegisteredIndicatorCanvas"));
		WidgetTree->RootWidget = Canvas;
	}

	VB_CLOG(!Canvas, Warning, "등록 표시기 갈래 %s: 루트가 캔버스가 아니다 - 요청을 그릴 자리가 없다", *GetName());
}

void UVBRegisteredIndicatorsWidget::SyncRequests(TConstArrayView<TPair<uint64, FVBIndicatorRequest>> Requests)
{
	TSet<uint64> Live;
	for (const TPair<uint64, FVBIndicatorRequest>& Entry : Requests)
	{
		Live.Add(Entry.Key);
		if (Shown.Contains(Entry.Key) || !Canvas)
		{
			continue;
		}

		const UClass* DataClass = Cast<UClass>(Entry.Value.Data.Get());
		if (!DataClass || !DataClass->IsChildOf(UVBRegisteredIndicatorWidget::StaticClass()) || DataClass->HasAnyClassFlags(CLASS_Abstract))
		{
			bool bAlreadyReported = false;
			ReportedBadData.Add(FObjectKey(Entry.Value.Data.Get()), &bAlreadyReported);
			VB_CLOG(!bAlreadyReported, Warning, "등록 표시기: 묶음 %s 의 요청이 그릴 위젯 클래스를 안 들었다(Data = %s) - 그리지 않는다",
				*Entry.Value.Group.ToString(), *GetNameSafe(Entry.Value.Data.Get()));
			continue;
		}

		UVBRegisteredIndicatorWidget* Child = CreateWidget<UVBRegisteredIndicatorWidget>(this, const_cast<UClass*>(DataClass));
		UCanvasPanelSlot* ChildSlot = Child ? Canvas->AddChildToCanvas(Child) : nullptr;
		if (!ChildSlot)
		{
			continue;
		}

		ChildSlot->SetAlignment(ChildAlignment);
		ChildSlot->SetAutoSize(true);
		// 첫 자리를 정하기 전에는 숨긴다 - 캔버스 원점에 한 프레임 번쩍이지 않게.
		Child->SetVisibility(ESlateVisibility::Collapsed);
		Shown.Add(Entry.Key, Child);
	}

	for (auto It = Shown.CreateIterator(); It; ++It)
	{
		if (!Live.Contains(It.Key()))
		{
			if (It.Value())
			{
				It.Value()->RemoveFromParent();
			}

			It.RemoveCurrent();
		}
	}
}

void UVBRegisteredIndicatorsWidget::UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot)
{
	const UWorld* World = GetWorld();
	const UVBIndicatorRegistrySubsystem* Registry = World ? World->GetSubsystem<UVBIndicatorRegistrySubsystem>() : nullptr;
	TArray<TPair<uint64, FVBIndicatorRequest>> Requests;
	if (Registry && View.PC)
	{
		for (const FVBIndicatorHandle& Handle : Registry->SnapshotForViewer(View.PC))
		{
			FVBIndicatorRequest Request;
			if (Registry->TryGetForViewer(Handle, View.PC, Request))
			{
				Requests.Emplace(Handle.GetId(), MoveTemp(Request));
			}
		}
	}

	SyncRequests(Requests);

	for (const TPair<uint64, FVBIndicatorRequest>& Entry : Requests)
	{
		UVBRegisteredIndicatorWidget* Child = Shown.FindRef(Entry.Key);
		UCanvasPanelSlot* ChildSlot = Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
		const USceneComponent* Anchor = Entry.Value.AnchorComponent.Get();
		if (!ChildSlot)
		{
			continue;
		}
		// 대상 위치를 따라간다. 숨김 요청이거나 앵커가 없거나 카메라 뒤(투영 실패)면 숨긴다.
		const FVBIndicatorPlacement Placement = Anchor && !Entry.Value.bSuppressed
			? VBWorldIndicatorProjection::Place(View, Anchor->GetSocketLocation(Entry.Value.AnchorSocket) + Entry.Value.WorldOffset,
				EVBIndicatorScreenPolicy::FollowWhenProjected, 0.0f)
			: FVBIndicatorPlacement();
		if (Placement.bVisible)
		{
			ChildSlot->SetPosition(Placement.Position);
		}

		Child->SetVisibility(Placement.bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

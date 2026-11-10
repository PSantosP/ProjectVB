// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Slots/VBUISlotWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/Slots/VBUISlotSubsystem.h"
#include "VB/VB.h"

void UVBUISlotWidget::SetSlot(const FGameplayTag& InSlotTag, const UObject* InOwner)
{
	SlotTag = InSlotTag;
	SlotOwner = InOwner;
}

const UObject* UVBUISlotWidget::FindSlotOwnerOf(const UWidget& Entry)
{
	const UPanelWidget* Parent = Entry.GetParent();
	const UVBUISlotWidget* Host = Parent ? Parent->GetTypedOuter<UVBUISlotWidget>() : nullptr;
	// 부모 패널이 그 슬롯의 SlotCanvas 일 때만 답한다 - 슬롯이 직접 붙인 항목만 소유자를 안다.
	return Host && Host->SlotCanvas == Parent ? Host->GetSlotOwner() : nullptr;
}

void UVBUISlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 자리 자체는 클릭을 먹지 않는다 - 빈 전체 화면 자리가 아래 게임 뷰포트의 입력을 가리면 안 된다. 항목은 스스로 정한다.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	BuildTreeIfEmpty();
}

void UVBUISlotWidget::BuildTreeIfEmpty()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		SlotCanvas = WidgetTree ? Cast<UCanvasPanel>(WidgetTree->RootWidget) : nullptr;
		// 블루프린트 자식이 자기 트리로 덮으면 루트가 캔버스가 아닐 수 있다 - 그러면 항목을 못 붙인다. 조용히 비지 않게 말한다.
		VB_CLOG(!SlotCanvas, Warning, "UI 슬롯 자리 %s: 루트가 CanvasPanel 이 아니라 항목을 붙일 수 없다", *GetName());
		return;
	}

	SlotCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SlotCanvas"));
	SlotCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = SlotCanvas;
}

void UVBUISlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!SlotTag.IsValid())
	{
		VB_LOG(Warning, "UI 슬롯 자리 %s: 태그 없이 구성됐다 - 붙이기 전에 SetSlot 을 불러야 한다", *GetName());
		return;
	}

	if (UVBUISlotSubsystem* Found = UVBUISlotSubsystem::Get(GetOwningPlayer()))
	{
		Registry = Found;
		Found->AddSlot(*this);
	}
}

void UVBUISlotWidget::NativeDestruct()
{
	if (UVBUISlotSubsystem* Found = Registry.Get())
	{
		Found->RemoveSlot(*this);
	}

	Registry.Reset();
	Super::NativeDestruct();
}

void UVBUISlotWidget::RefreshEntries()
{
	if (bApplying)
	{
		bDirty = true;
		return;
	}

	bApplying = true;
	// 적용 중 위젯 구성이 등록, 해제, 자리 떼기를 부를 수 있다. 그때는 표시만 남기고, 끝난 뒤 최신 목록으로 다시 맞춘다.
	int32 Rounds = 0;
	do
	{
		bDirty = false;
		UVBUISlotSubsystem* Found = Registry.Get();
		if (!Found || !Found->HasSlot(*this))
		{
			break;
		}

		ApplyEntries(Found->GetEntriesFor(*this));
		// 매 반복이 적어도 한 변경을 따라간다. 끝없이 서로 등록, 해제하는 위젯이면 여기서 멈추고 말한다 -
		//   그때 자리는 마지막 적용 상태로 남는다(의도: 무한 반복보다 낡은 화면이 낫다, 경고로 드러난다).
		if (++Rounds > EntryWidgets.Num() + 8)
		{
			VB_LOG(Warning, "UI 슬롯 자리 %s(%s): 항목 구성이 등록을 계속 바꾼다 - 맞추기를 멈춘다", *GetName(), *SlotTag.ToString());
			bDirty = false;
		}
	}

	while (bDirty);
	bApplying = false;
}

void UVBUISlotWidget::ApplyEntries(const TArray<FVBUISlotEntry>& Entries)
{
	if (!SlotCanvas)
	{
		return;
	}
	// 사라진 항목은 떼기만 한다. 남은 항목은 손대지 않는다(떼었다 붙이면 그 위젯이 해체, 구성된다).
	TSet<int32> Wanted;
	for (const FVBUISlotEntry& Entry : Entries)
	{
		Wanted.Add(Entry.Id);
	}

	for (auto It = EntryWidgets.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()))
		{
			if (UUserWidget* Old = It.Value())
			{
				Old->RemoveFromParent();
			}

			It.RemoveCurrent();
		}
	}

	for (const FVBUISlotEntry& Entry : Entries)
	{
		UUserWidget* Widget = EntryWidgets.FindRef(Entry.Id);
		bool bCreated = false;
		if (!Widget)
		{
			Widget = CreateWidget<UUserWidget>(GetOwningPlayer(), Entry.WidgetClass);
			if (!Widget)
			{
				// 항목마다 한 번만 말한다 - 맞출 때마다 다시 만들려다 같은 경고가 쌓이지 않게.
				bool bAlreadyWarned = false;
				FailedEntryIds.Add(Entry.Id, &bAlreadyWarned);
				VB_CLOG(!bAlreadyWarned, Warning, "UI 슬롯 자리 %s: %s 를 만들지 못했다(추상 클래스?)", *SlotTag.ToString(),
					*GetNameSafe(Entry.WidgetClass));
				continue;
			}
			// 붙이기 전에 지도에 넣는다 - 붙이는 중의 중첩 맞추기가 같은 항목을 또 만들지 않는다.
			EntryWidgets.Add(Entry.Id, Widget);
			bCreated = true;
		}

		UCanvasPanelSlot* EntrySlot = Cast<UCanvasPanelSlot>(Widget->Slot);
		if (!EntrySlot || EntrySlot->Parent != SlotCanvas)
		{
			EntrySlot = SlotCanvas->AddChildToCanvas(Widget);
			if (!EntrySlot)
			{
				continue;
			}

			EntrySlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			EntrySlot->SetOffsets(FMargin(0.0f));
			EntrySlot->SetAutoSize(true);
		}
		// 살아 있는 Slate 순서도 이것으로 바뀐다(SConstraintCanvas 가 ZOrder 로 정렬). 같은 Order 는 붙인 순서.
		if (EntrySlot->GetZOrder() != Entry.Order)
		{
			EntrySlot->SetZOrder(Entry.Order);
		}
		// 처음 보임은 새로 만든 항목에만, 붙인 뒤(구성 뒤)에 SetVisibility 한다.
		if (bCreated && Entry.bHasInitialVisibility)
		{
			Widget->SetVisibility(Entry.InitialVisibility);
		}
	}
}

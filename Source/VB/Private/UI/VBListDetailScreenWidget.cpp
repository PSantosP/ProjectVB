// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBListDetailScreenWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "UI/VBListRowWidget.h"
#include "VB/VB.h"

UWidget* UVBListDetailScreenWidget::NativeGetDesiredFocusTarget() const
{
	// 게임패드 초점은 첫 줄의 버튼이다. 목록이 비면 부모 규칙을 따른다.
	if (EntryBox && EntryBox->GetChildrenCount() > 0)
	{
		if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(0)))
		{
			return Row->GetButton();
		}
	}

	return Super::NativeGetDesiredFocusTarget();
}

void UVBListDetailScreenWidget::Rebuild()
{
	RebuildRows();
	RequestRefreshFocus();
}

void UVBListDetailScreenWidget::RebuildRows()
{
	if (EntryBox)
	{
		EntryBox->ClearChildren();
	}

	const int32 RowCount = BuildRows();
	VB_CLOG(!EntryWidgetClass && RowCount > 0, Warning, "%s: EntryWidgetClass 미지정 - 목록을 그릴 수 없다(WBP 클래스 기본값)", *GetClass()->GetName());
	if (TxtEmpty)
	{
		TxtEmpty->SetVisibility(RowCount == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	RefreshDetail();
	WireDetailNavigation();
}

UVBListRowWidget* UVBListDetailScreenWidget::AddRow(int32 RowIndex, const FText& Label, const FText& Detail)
{
	if (!EntryBox || !EntryWidgetClass)
	{
		return nullptr;
	}

	UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, EntryWidgetClass);
	// 못 하는 줄도 고를 수는 있다. 고르면 상세가 까닭을 보여 준다.
	Row->Setup(RowIndex, Label, Detail, /*bEnabled=*/true);
	Row->OnRowClicked.BindUObject(this, &UVBListDetailScreenWidget::HandleRowClicked);
	EntryBox->AddChild(Row);
	return Row;
}

void UVBListDetailScreenWidget::HandleRowClicked(int32 RowIndex)
{
	LastClickedRow = RowIndex;
	Select(RowIndex);
	if (UWidget* Action = FirstUsableAction())
	{
		Action->SetFocus();
	}
}

UWidget* UVBListDetailScreenWidget::FirstUsableAction() const
{
	for (UWidget* Action : GetDetailActions())
	{
		if (Action && Action->IsVisible() && Action->GetIsEnabled())
		{
			return Action;
		}
	}

	return nullptr;
}

UWidget* UVBListDetailScreenWidget::NavigateToAction(EUINavigation Direction)
{
	return FirstUsableAction();
}

UWidget* UVBListDetailScreenWidget::NavigateToRow(EUINavigation Direction)
{
	UWidget* FirstRow = nullptr;
	const int32 RowCount = EntryBox ? EntryBox->GetChildrenCount() : 0;
	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(Index));
		UWidget* RowButton = Row ? Row->GetButton() : nullptr;
		if (RowButton && Row->GetRowIndex() == LastClickedRow)
		{
			return RowButton;
		}

		FirstRow = FirstRow ? FirstRow : RowButton;
	}

	return FirstRow;
}

void UVBListDetailScreenWidget::WireDetailNavigation()
{
	FCustomWidgetNavigationDelegate ToAction;
	ToAction.BindDynamic(this, &UVBListDetailScreenWidget::NavigateToAction);
	const int32 RowCount = EntryBox ? EntryBox->GetChildrenCount() : 0;
	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(Index));
		if (UWidget* RowButton = Row ? Row->GetButton() : nullptr)
		{
			RowButton->SetNavigationRuleCustom(EUINavigation::Right, ToAction);
			if (Index == RowCount - 1)
			{
				RowButton->SetNavigationRuleCustom(EUINavigation::Down, ToAction);
			}
		}
	}

	FCustomWidgetNavigationDelegate ToRow;
	ToRow.BindDynamic(this, &UVBListDetailScreenWidget::NavigateToRow);
	for (UWidget* Action : GetDetailActions())
	{
		if (Action)
		{
			Action->SetNavigationRuleCustom(EUINavigation::Left, ToRow);
		}
	}
}

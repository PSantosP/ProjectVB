// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBMenuScreenWidget.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Components/WidgetSwitcher.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VB/VB.h"

bool UVBMenuScreenWidget::SwitchToPage(int32 PageIndex)
{
	if (!MenuSwitcher)
	{
		VB_LOG(Warning, "%s: MenuSwitcher 미바인딩 - 화면 전환 불가. WBP 위젯 이름 확인", *GetName());
		return false;
	}

	MenuSwitcher->SetActiveWidgetIndex(PageIndex);
	return true;
}

void UVBMenuScreenWidget::ShowSettingsPage()
{
	// 설정은 메뉴 안 칸이 아니라 따로 뜨는 옵션 화면이다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_Options);
	}
}

void UVBMenuScreenWidget::ShowSlotPage(EVBSlotListMode InMode)
{
	if (!SwitchToPage(PageIndex_Slots))
	{
		return;
	}

	if (SlotListView)
	{
		// SetListMode 안에서 목록을 다시 만든다. 저장/삭제로 슬롯이 바뀌었을 수 있으므로
		// 재진입 때마다 새로 그려야 한다 - 스위처는 페이지를 파괴하지 않는다.
		SlotListView->SetListMode(InMode);
	}
	else
	{
		VB_LOG(Warning, "%s: SlotListView 미바인딩 - 슬롯 화면이 빈 채로 열린다", *GetName());
	}
}

void UVBMenuScreenWidget::ShowMainPage()
{
	SwitchToPage(PageIndex_Main);
}

void UVBMenuScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (MenuSwitcher)
	{
		MenuSwitcher->SetActiveWidgetIndex(PageIndex_Main);
	}
}

bool UVBMenuScreenWidget::NativeOnHandleBackAction()
{
	if (MenuSwitcher && MenuSwitcher->GetActiveWidgetIndex() != PageIndex_Main)
	{
		ShowMainPage();
		return true;
	}

	return Super::NativeOnHandleBackAction();
}

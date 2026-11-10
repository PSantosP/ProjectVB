// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "UI/VBSaveSlotListWidget.h"
#include "VBMenuScreenWidget.generated.h"

/** 스위처로 하위 화면(첫 화면, 슬롯 목록)을 갈아 끼우는 메뉴의 공통 토대. 메인, 일시정지 메뉴가 쓴다. */
UCLASS(Abstract)
class VB_API UVBMenuScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	// 옵션 화면(UI.Screen.Options)을 따로 띄운다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ShowSettingsPage();

	// 슬롯 화면으로 전환하며 용도를 지정한다(목록도 다시 만든다).
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ShowSlotPage(EVBSlotListMode InMode);

	// 메뉴 첫 화면으로 복귀. 하위 화면의 Back 버튼이 부른다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ShowMainPage();

protected:
	// 페이지 인덱스: 0=첫 화면, 1=슬롯 목록.
	static constexpr int32 PageIndex_Main = 0;
	static constexpr int32 PageIndex_Slots = 1;

	// 공통 전환. 스위처가 없으면 경고 후 false.
	bool SwitchToPage(int32 PageIndex);

	// 뒤로 가기: 슬롯 페이지면 첫 화면으로, 첫 화면이면 부모에 맡긴다.
	virtual bool NativeOnHandleBackAction() override;

	// 켜질 때마다 첫 페이지에서 시작한다(화면 위젯은 재사용된다).
	virtual void NativeOnActivated() override;

	// 하위 화면 스위처.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UWidgetSwitcher> MenuSwitcher;

	// 스위처 1번 페이지의 슬롯 목록.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UVBSaveSlotListWidget> SlotListView;
};

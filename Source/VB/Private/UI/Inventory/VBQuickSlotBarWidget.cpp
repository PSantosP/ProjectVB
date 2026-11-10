// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Inventory/VBQuickSlotBarWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Inventory/VBInventoryComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/VBInputDeviceManager.h"
#include "Inventory/VBItemDefinition.h"
#include "UI/VBInputKeyText.h"
#include "VB/VB.h"

void UVBQuickSlotWidget::SetSlot(const UVBItemDefinition* Item, int32 Count, const FText& KeyText, const TOptional<FSlateBrush>& KeyIcon)
{
	const bool bHasIcon = Item && !Item->Icon.IsNull() && ImgIcon;
	if (ImgIcon)
	{
		if (bHasIcon)
		{
			// 소프트 참조라 비동기로 불러 그린다 - HUD 가 칸을 그리느라 멈추지 않는다.
			ImgIcon->SetBrushFromSoftTexture(Item->Icon);
		}

		ImgIcon->SetVisibility(bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (TxtName)
	{
		// 그림이 없으면 이름이 그 자리를 맡는다.
		TxtName->SetText(Item ? Item->DisplayName : FText::GetEmpty());
		TxtName->SetVisibility(Item && !bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (TxtCount)
	{
		TxtCount->SetText(Item ? FText::AsNumber(Count) : FText::GetEmpty());
		TxtCount->SetVisibility(Item ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	VBInputKeyText::ShowKey(TxtKey, ImgKey, KeyText, KeyIcon);
	SetRenderOpacity(Item && Count > 0 ? 1.0f : DimmedOpacity);
}

void UVBQuickSlotBarWidget::SetInventory(UVBInventoryComponent* InInventory)
{
	Unbind();
	Inventory = InInventory;
	if (InInventory)
	{
		InventoryChangedHandle = InInventory->OnInventoryChanged.AddUObject(this, &UVBQuickSlotBarWidget::Refresh);
		QuickSlotsChangedHandle = InInventory->OnQuickSlotsChanged.AddUObject(this, &UVBQuickSlotBarWidget::Refresh);
	}

	Refresh();
}

void UVBQuickSlotBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성될 때마다 다시 잇는다(해체 때 푼다).
	PlayerStateSet.FollowComponent(this, Inventory, &UVBQuickSlotBarWidget::SetInventory);
	if (AVBPlayerController* PC = GetOwningPlayer<AVBPlayerController>())
	{
		// 장치 관리자는 폰에 있다 - 폰이 바뀌면 새 폰의 것을 듣는다.
		PawnSource = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UVBQuickSlotBarWidget::HandlePossessedPawnChanged);
		BindDeviceManager(PC->GetPawn());
	}

	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
		{
			InputRouter = Router;
			InputModeHandle = Router->OnActiveInputModeChanged().AddUObject(this, &UVBQuickSlotBarWidget::HandleInputModeChanged);
		}
	}

	RefreshKeyTexts();
}

void UVBQuickSlotBarWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	Unbind();
	if (AVBPlayerController* PC = PawnSource.Get())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBQuickSlotBarWidget::HandlePossessedPawnChanged);
	}

	PawnSource.Reset();
	if (UVBInputDeviceManager* Manager = DeviceManager.Get())
	{
		Manager->OnInputDeviceChanged.RemoveDynamic(this, &UVBQuickSlotBarWidget::HandleInputDeviceChanged);
	}

	DeviceManager.Reset();
	if (UCommonUIActionRouterBase* Router = InputRouter.Get())
	{
		Router->OnActiveInputModeChanged().Remove(InputModeHandle);
	}

	InputRouter.Reset();
	InputModeHandle.Reset();
	Super::NativeDestruct();
}

void UVBQuickSlotBarWidget::HandlePossessedPawnChanged(APawn* /*OldPawn*/, APawn* NewPawn)
{
	BindDeviceManager(NewPawn);
	RefreshKeyTexts();
}

void UVBQuickSlotBarWidget::BindDeviceManager(APawn* Pawn)
{
	UVBInputDeviceManager* Found = UVBInputDeviceManager::FindFor(Pawn);
	if (Found == DeviceManager.Get())
	{
		return;
	}

	if (UVBInputDeviceManager* Old = DeviceManager.Get())
	{
		Old->OnInputDeviceChanged.RemoveDynamic(this, &UVBQuickSlotBarWidget::HandleInputDeviceChanged);
	}

	DeviceManager = Found;
	if (Found)
	{
		Found->OnInputDeviceChanged.AddUniqueDynamic(this, &UVBQuickSlotBarWidget::HandleInputDeviceChanged);
		InputType = Found->GetCurrentDeviceType();
	}
}

void UVBQuickSlotBarWidget::HandleInputDeviceChanged(ECommonInputType NewDeviceType)
{
	InputType = NewDeviceType;
	RefreshKeyTexts();
}

void UVBQuickSlotBarWidget::HandleInputModeChanged(ECommonInputMode NewMode)
{
	// 메뉴를 닫고 돌아왔다 - 그 사이 설정 화면에서 키를 바꿨을 수 있다. 다시 지어진 매핑에서 키 이름을 다시 읽는다.
	if (NewMode != ECommonInputMode::Menu)
	{
		RefreshKeyTexts();
	}
}

void UVBQuickSlotBarWidget::RefreshKeyTexts()
{
	const AVBPlayerController* PC = GetOwningPlayer<AVBPlayerController>();
	const int32 ActionCount = PC ? PC->GetQuickSlotActionCount() : 0;
	TArray<FText> Texts;
	TArray<TOptional<FSlateBrush>> Icons;
	int32 Unmapped = 0;
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		bool bUnmapped = false;
		Texts.Add(VBInputKeyText::ForAction(PC, PC->GetQuickSlotAction(Index), InputType, bUnmapped));
		Icons.Add(VBInputKeyText::IconForAction(PC, PC->GetQuickSlotAction(Index)));
		Unmapped += bUnmapped ? 1 : 0;
	}
	// 키 1~4 매핑이 없는 빌드에서는 키 없는 칸이 정상이라 경고가 아니라 기록 한 줄이다.
	VB_CLOG(Unmapped > 0, Log, "퀵슬롯 칸 줄: 키 %d 중 %d 개는 지금 IMC 에 키가 없다 - 칸에 키 이름 없음", ActionCount, Unmapped);
	KeyTexts = MoveTemp(Texts);
	KeyIcons = MoveTemp(Icons);
	Refresh();
}

void UVBQuickSlotBarWidget::Refresh()
{
	if (!SlotBox)
	{
		return;
	}

	const UVBInventoryComponent* Owned = Inventory.Get();
	const int32 SlotCount = Owned ? Owned->GetQuickSlots().Num() : 0;
	if (SlotCount > 0 && !SlotWidgetClass)
	{
		VB_LOG(Warning, "퀵슬롯 칸 줄: SlotWidgetClass 미지정 - 칸을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}
	// 칸 수가 바뀔 때만 다시 짓는다 - 가진 수, 키 이름이 바뀌는 매번 위젯을 새로 만들지 않는다.
	if (SlotBox->GetChildrenCount() != SlotCount)
	{
		SlotBox->ClearChildren();
		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			SlotBox->AddChild(CreateWidget<UVBQuickSlotWidget>(this, SlotWidgetClass));
		}
	}

	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		if (UVBQuickSlotWidget* SlotWidget = Cast<UVBQuickSlotWidget>(SlotBox->GetChildAt(Index)))
		{
			const UVBItemDefinition* Item = Owned->GetQuickSlotItem(Index);
			SlotWidget->SetSlot(Item, Item ? Owned->GetCount(Item) : 0, KeyTexts.IsValidIndex(Index) ? KeyTexts[Index] : FText::GetEmpty(),
				KeyIcons.IsValidIndex(Index) ? KeyIcons[Index] : TOptional<FSlateBrush>());
		}
	}
}

void UVBQuickSlotBarWidget::Unbind()
{
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		Owned->OnInventoryChanged.Remove(InventoryChangedHandle);
		Owned->OnQuickSlotsChanged.Remove(QuickSlotsChangedHandle);
	}

	InventoryChangedHandle.Reset();
	QuickSlotsChangedHandle.Reset();
	Inventory.Reset();
}

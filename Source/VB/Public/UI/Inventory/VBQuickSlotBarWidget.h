// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CommonInputModeTypes.h"  // ECommonInputMode - 입력 모드 바뀜 인자
#include "CommonInputTypeEnum.h"   // ECommonInputType - 장치 바뀜 인자
#include "Styling/SlateBrush.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBQuickSlotBarWidget.generated.h"

class APawn;
class UCommonUIActionRouterBase;
class UImage;
class UVBInputDeviceManager;
class UPanelWidget;
class UTextBlock;
class UVBInventoryComponent;
class UVBItemDefinition;

/** 퀵슬롯 한 칸: 아이콘(없으면 이름), 가진 수, 키 이름. 칸 줄이 만들고 채운다. */
UCLASS(Abstract)
class VB_API UVBQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Item = nullptr 이면 빈 칸, Count 0 이면 흐리게. 키는 그림 > 글자 > 접기 순이다.
	void SetSlot(const UVBItemDefinition* Item, int32 Count, const FText& KeyText, const TOptional<FSlateBrush>& KeyIcon);

protected:
	// 가진 것이 0 인 칸, 빈 칸의 불투명도.
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DimmedOpacity = 0.35f;

	// UMG 이름 바인딩. 이름, 수, 키는 필수, 아이콘은 선택이다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgIcon;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtName;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtCount;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtKey;

	// 키 그림 칸(선택). 없으면 키 이름 글자만 쓴다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgKey;
};

/**
 * HUD 의 퀵슬롯 칸 줄. 인벤토리의 칸, 수 변경을 듣고 다시 그린다(칸 수도 인벤토리가 정한다).
 * 키 이름은 장치가 바뀌거나 게임 입력으로 돌아올 때 다시 읽는다.
 */
UCLASS(Abstract)
class VB_API UVBQuickSlotBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 그릴 인벤토리. 보통은 구성 때 스스로 찾으므로 다른 소유자, 시험용이다.
	void SetInventory(UVBInventoryComponent* InInventory);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 한 칸의 WBP. 비면 칸이 안 그려지고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TSubclassOf<UVBQuickSlotWidget> SlotWidgetClass;

	// 칸들이 들어갈 자리(필수).
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> SlotBox;

private:
	friend class FVBHUDRowTestAccess;

	void Refresh();
	void Unbind();

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION()
	void HandleInputDeviceChanged(ECommonInputType NewDeviceType);
	void HandleInputModeChanged(ECommonInputMode NewMode);
	void BindDeviceManager(APawn* Pawn);
	void RefreshKeyTexts();

	FVBPlayerStateBinding PlayerStateSet;
	TWeakObjectPtr<UVBInventoryComponent> Inventory;
	FDelegateHandle InventoryChangedHandle;
	FDelegateHandle QuickSlotsChangedHandle;
	TArray<FText> KeyTexts; // 칸 번호 -> 키 이름
	TArray<TOptional<FSlateBrush>> KeyIcons; // KeyTexts 와 같은 길이 - 칸 번호 -> 키 그림

	// 구독한 출처. 풀 때 다시 찾지 않고 여기서 푼다.
	TWeakObjectPtr<AVBPlayerController> PawnSource;
	TWeakObjectPtr<UVBInputDeviceManager> DeviceManager;
	TWeakObjectPtr<UCommonUIActionRouterBase> InputRouter;
	FDelegateHandle InputModeHandle;
	ECommonInputType InputType = ECommonInputType::MouseAndKeyboard;
};

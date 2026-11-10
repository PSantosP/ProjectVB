// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBMainMenuWidget.h"

#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Player/VBMenuPlayerController.h"
#include "UI/VBSaveSlotListWidget.h"
#include "VB/VB.h"
#include "Components/WidgetSwitcher.h"
#include "Kismet/KismetSystemLibrary.h"

UVBMainMenuWidget::UVBMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 메뉴 레벨의 바닥 화면. 뒤로 가기는 하위 페이지에서만 쓰고 첫 화면에서는 닫지 않는다.
	InputMode = EVBScreenInputMode::Menu;
	bIsBackHandler = true;
}

bool UVBMainMenuWidget::NativeOnHandleBackAction()
{
	// 하위 페이지면 부모가 첫 화면으로 되돌린다. 첫 화면이면 끄지 않고 입력만 먹는다.
	if (MenuSwitcher && MenuSwitcher->GetActiveWidgetIndex() != PageIndex_Main)
	{
		return Super::NativeOnHandleBackAction();
	}

	return true;
}

// "가장 최근 슬롯" 정책은 UVBSaveGameSubsystem 이 소유한다. 아래 둘은 위임이다.
bool UVBMainMenuWidget::HasAnySaveGame() const
{
	const UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	return Subsystem ? Subsystem->HasAnySaveGame() : false;
}

int32 UVBMainMenuWidget::GetMostRecentSlotIndex() const
{
	const UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	return Subsystem ? Subsystem->GetMostRecentSlotIndex() : INDEX_NONE;
}

void UVBMainMenuWidget::ContinueMostRecent()
{
	UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const int32 RecentSlot = GetMostRecentSlotIndex();
	if (RecentSlot < 0)
	{
		return;
	}

	// 로드 완료 후 레벨을 열기 위해 1회용으로 델리게이트 구독.
	Subsystem->OnGameLoadCompleted.AddDynamic(this, &UVBMainMenuWidget::HandleLoadCompleted);
	Subsystem->LoadGameSlot(RecentSlot);
}

bool UVBMainMenuWidget::StartNewGame()
{
	UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	if (!Subsystem)
	{
		return false;
	}

	// 슬롯은 처음 저장할 때 정해진다. 난이도 고르기를 열고, 시작은 그 화면의 확인이 한다.
	if (AVBMenuPlayerController* MenuPC = Cast<AVBMenuPlayerController>(GetOwningPlayer()))
	{
		return MenuPC->RequestNewGame(INDEX_NONE);
	}
	// 난이도를 건너뛴 시작으로 덮지 않는다.
	VB_LOG(Error, "메인 메뉴: 주인이 메뉴 PC 가 아니다 - 새 게임을 시작하지 않는다");
	return false;
}

void UVBMainMenuWidget::HandleLoadCompleted(int32 SlotIndex, bool bSuccess)
{
	UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	if (Subsystem)
	{
		// 1회용 구독 해제(중복 호출 방지).
		Subsystem->OnGameLoadCompleted.RemoveDynamic(this, &UVBMainMenuWidget::HandleLoadCompleted);
	}

	if (bSuccess && Subsystem)
	{
		Subsystem->TravelToGameplayLevel();
	}
}

void UVBMainMenuWidget::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void UVBMainMenuWidget::ShowLoadPage()
{
	ShowSlotPage(EVBSlotListMode::Load);
}

UVBSaveGameSubsystem* UVBMainMenuWidget::GetSaveSubsystem() const
{
	return UVBSaveGameSubsystem::Get(this);
}

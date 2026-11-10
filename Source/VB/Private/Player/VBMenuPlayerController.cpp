// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Player/VBMenuPlayerController.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Settings/VBGameUserSettings.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VB/VB.h"

void AVBMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	if (UVBGameUserSettings* Machine = UVBGameUserSettings::Get())
	{
		Machine->SyncMenuFrameRateRequest(MenuFrameRateRequest, true, TEXT("MainMenu"));
	}
	else
	{
		// 설정이 깨진 것이다(DefaultEngine.ini GameUserSettingsClassName). 메뉴 제한 없이 사용자 값만 걸린다.
		VB_LOG(Warning, "[VBMenuPlayerController] 기계 설정이 UVBGameUserSettings 가 아니다 - 메인 메뉴 프레임 제한이 걸리지 않는다");
	}
	// 메인 메뉴 화면을 연다. 입력 모드, 커서, 초점은 그 화면 설정을 CommonUI 가 적용한다 -
	//   여기서 SetInputMode(UIOnly) 를 걸면 입력 모드를 관리하는 쪽이 둘이 된다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_MainMenu);
	}
	else
	{
		// 조용한 빈 화면 방지.
		VB_LOG(Warning, "[VBMenuPlayerController] UI 관리자를 못 찾았다 — 메뉴가 표시되지 않습니다.");
	}
}

void AVBMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MenuFrameRateRequest.IsValid())
	{
		if (UVBGameUserSettings* Machine = UVBGameUserSettings::Get())
		{
			Machine->RemoveMenuFrameRateRequest(MenuFrameRateRequest);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool AVBMenuPlayerController::RequestNewGame(int32 SlotIndex)
{
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this);
	if (!UI)
	{
		// 메인 메뉴도 같은 관리자로 뜬다(BeginPlay) - 여기서 없으면 메뉴부터 고장이다.
		VB_LOG(Error, "[VBMenuPlayerController] UI 관리자가 없어 난이도 고르기를 못 연다 - 새 게임을 시작하지 않는다");
		return false;
	}
	// 떠 있는 난이도 선택 화면은 다시 켜지지 않아 요청을 가져가지 않는다 - 덮어 두면 처리할 쪽이 없는 슬롯이 남는다.
	if (UI->IsScreenOpen(VBGameplayTags::UI_Screen_DifficultyPicker))
	{
		return true;
	}

	PendingNewGameSlot = SlotIndex;
	UI->PushScreen(VBGameplayTags::UI_Screen_DifficultyPicker);
	return true;
}

TOptional<int32> AVBMenuPlayerController::ConsumePendingNewGame()
{
	TOptional<int32> Slot = PendingNewGameSlot;
	PendingNewGameSlot.Reset();
	return Slot;
}

bool AVBMenuPlayerController::StartNewGameNow(int32 SlotIndex, FGameplayTag Difficulty)
{
	UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	if (!Save)
	{
		VB_LOG(Warning, "[VBMenuPlayerController] 저장 서브시스템이 없어 새 게임을 시작하지 못한다");
		return false;
	}

	const bool bAccepted = SlotIndex == INDEX_NONE ? Save->StartNewGameWithDifficulty(Difficulty)
		: Save->StartNewGameInSlotWithDifficulty(SlotIndex, Difficulty);
	VB_CLOG(!bAccepted, Warning, "[VBMenuPlayerController] 새 게임 시작이 거절됐다(슬롯 %d - 시작 중·정지 중이거나 슬롯 범위 밖) - 레벨을 열지 않는다", SlotIndex);
	if (bAccepted)
	{
		Save->TravelToGameplayLevel();
	}

	return bAccepted;
}

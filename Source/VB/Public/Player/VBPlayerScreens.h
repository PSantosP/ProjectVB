// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class APlayerController;
class UVBCraftingComponent;
class UVBDialogueComponent;
class UVBFastTravelComponent;
class UVBShopComponent;
class UVBUIManagerSubsystem;

// 플레이어의 화면 요청을 UI 관리자에 전달하고, 화면이 켜질 때 고를 항목을 잠깐 보관한다. 화면과 입력 정책은 UI 관리자의 몫이다.
class FVBPlayerScreens
{
public:
	// HUD 화면을 연다. UI 관리자가 없으면 경고한다.
	void OpenHUD(const APlayerController& Controller);

	// 대화, 상점, 제작, 빠른 이동의 보기가 바뀌면 그 화면을 맞춘다. 화면 태그를 아는 게임 쪽 층이 여기 하나다.
	void FollowViews(APlayerController& Controller, UVBDialogueComponent* Dialogue, UVBShopComponent* Shop, UVBCraftingComponent* Crafting,
		UVBFastTravelComponent* FastTravel);

	// 메뉴 화면 하나를 연다.
	void OpenMenu(const APlayerController& Controller, const FGameplayTag& ScreenTag);

	// 도감을 연다. 사실을 주면 그 항목을 고른 채 연다.
	void OpenCollection(const APlayerController& Controller, const FGameplayTag& DiscoveryFact);

	// 튜토리얼 다시 보기를 연다. ID 를 주면 그 안내를 고른 채 연다.
	void OpenTutorial(const APlayerController& Controller, const FGameplayTag& TutorialId);

	// 화면이 켜질 때 한 번 가져간다(가져가면 비운다).
	FGameplayTag ConsumeCollectionEntry();
	FGameplayTag ConsumeTutorialId();

	// 고를 항목이 새로 왔는데 그 화면이 이미 켜져 있을 때 알린다. 화면을 부르지 않고 화면이 듣는다.
	FSimpleMulticastDelegate OnSelectionPending;

	// 사망 연출이 시작될 때 도감과 튜토리얼을 내리고 고를 항목을 비운다.
	void CloseSelectionScreens(const APlayerController& Controller);

	// 레벨을 떠나기 전 메뉴 화면을 모두 내린다 - 정지가 풀린다. 입력 모드는 다음 레벨 HUD 가 맞춘다.
	void CloseAllMenus(const APlayerController& Controller);

private:
	void ClearSelections();

	// 켜진 화면이면 알리고, 가려졌으면 내렸다 올리고, 없으면 연다.
	void OpenWithSelection(UVBUIManagerSubsystem& UI, const FGameplayTag& ScreenTag);

	// 도감 화면이 켜질 때 고를 항목(로컬 전용, 복제 없음).
	FGameplayTag PendingCollectionEntry;

	// 튜토리얼 화면이 켜질 때 고를 안내(로컬 전용).
	FGameplayTag PendingTutorialId;
};

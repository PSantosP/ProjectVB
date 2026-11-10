// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Player/VBPlayerScreens.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "CommonActivatableWidget.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Map/VBFastTravelComponent.h"
#include "Narrative/Collection/VBCollectionEntryDefinition.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VB/VB.h"

namespace
{
	void SyncScreen(const APlayerController& Controller, const FGameplayTag& ScreenTag, bool bWanted)
	{
		// 이미 떠 있으면 PushScreen 이, 안 떠 있으면 PopScreen 이 아무것도 안 한다 - 보기가 여러 번 바뀌어도 한 줄로 맞춘다.
		if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
		{
			if (bWanted)
			{
				UI->PushScreen(ScreenTag);
			}
			else
			{
				UI->PopScreen(ScreenTag);
			}
		}
	}

	// 컴포넌트는 보기 바뀜만 알린다 - 화면을 여닫는 일은 PC 가 살아 있는 동안 여기서 한다.
	template <typename ComponentType>
	void FollowView(APlayerController& Controller, ComponentType* Component, const FGameplayTag& ScreenTag)
	{
		APlayerController* ControllerPtr = &Controller;
		Component->OnViewChanged.AddWeakLambda(ControllerPtr, [ControllerPtr, Component, ScreenTag]()
		{
			SyncScreen(*ControllerPtr, ScreenTag, Component->GetView().IsActive());
		});
	}
}

void FVBPlayerScreens::OpenHUD(const APlayerController& Controller)
{
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_HUD);
	}
	else
	{
		VB_LOG(Warning, "[VBPC] UI 관리자를 못 찾았다 - HUD 가 안 뜬다");
	}
}

void FVBPlayerScreens::FollowViews(APlayerController& Controller, UVBDialogueComponent* Dialogue, UVBShopComponent* Shop,
	UVBCraftingComponent* Crafting, UVBFastTravelComponent* FastTravel)
{
	FollowView(Controller, Dialogue, VBGameplayTags::UI_Screen_Dialogue);
	FollowView(Controller, Shop, VBGameplayTags::UI_Screen_Shop);
	FollowView(Controller, Crafting, VBGameplayTags::UI_Screen_Crafting);
	FollowView(Controller, FastTravel, VBGameplayTags::UI_Screen_FastTravel);
}

void FVBPlayerScreens::OpenMenu(const APlayerController& Controller, const FGameplayTag& ScreenTag)
{
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
	{
		UI->PushScreen(ScreenTag);
	}
}

void FVBPlayerScreens::OpenCollection(const APlayerController& Controller, const FGameplayTag& DiscoveryFact)
{
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller);
	if (!UI)
	{
		return;
	}
	// 일반 열기도 빈 태그로 덮는다 - 지난 요청의 선택이 다음 열기로 새지 않게.
	PendingCollectionEntry = VBCollection::IsEntryFact(DiscoveryFact) ? DiscoveryFact : FGameplayTag();

	OpenWithSelection(*UI, VBGameplayTags::UI_Screen_Collection);
}

void FVBPlayerScreens::OpenTutorial(const APlayerController& Controller, const FGameplayTag& TutorialId)
{
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller);
	if (!UI)
	{
		return;
	}
	// 도감과 같은 구조(OpenCollection). 일반 열기도 빈 태그로 덮는다 - 지난 요청의 선택이 다음 열기로 새지 않게.
	PendingTutorialId = TutorialId;

	OpenWithSelection(*UI, VBGameplayTags::UI_Screen_Tutorial);
}

void FVBPlayerScreens::OpenWithSelection(UVBUIManagerSubsystem& UI, const FGameplayTag& ScreenTag)
{
	UCommonActivatableWidget* Existing = UI.FindOpenScreen(ScreenTag);
	if (Existing && Existing->IsActivated())
	{
		OnSelectionPending.Broadcast();
		return;
	}
	// 쌓여 있지만 가려진 화면은 내렸다 다시 올린다 - 이미 떠 있다고 요청이 무시되면 선택이 사라진다.
	if (Existing)
	{
		UI.PopScreen(ScreenTag);
	}

	UI.PushScreen(ScreenTag);
}

FGameplayTag FVBPlayerScreens::ConsumeCollectionEntry()
{
	const FGameplayTag Entry = PendingCollectionEntry;
	PendingCollectionEntry = FGameplayTag();
	return Entry;
}

FGameplayTag FVBPlayerScreens::ConsumeTutorialId()
{
	const FGameplayTag Id = PendingTutorialId;
	PendingTutorialId = FGameplayTag();
	return Id;
}

void FVBPlayerScreens::CloseSelectionScreens(const APlayerController& Controller)
{
	ClearSelections();
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
	{
		UI->PopScreen(VBGameplayTags::UI_Screen_Collection);
		UI->PopScreen(VBGameplayTags::UI_Screen_Tutorial);
	}
}

void FVBPlayerScreens::CloseAllMenus(const APlayerController& Controller)
{
	ClearSelections();
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
	{
		UI->PopScreen(VBGameplayTags::UI_Screen_Collection);
		UI->PopScreen(VBGameplayTags::UI_Screen_Tutorial);
		UI->PopScreen(VBGameplayTags::UI_Screen_Pause);
		UI->PopScreen(VBGameplayTags::UI_Screen_GameOver);
	}
}

void FVBPlayerScreens::ClearSelections()
{
	PendingCollectionEntry = FGameplayTag();
	PendingTutorialId = FGameplayTag();
}

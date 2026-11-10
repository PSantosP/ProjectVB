// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBPauseMenuWidget.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerState.h"
#include "Player/VBPlayerController.h"
#include "UI/VBReputationBarWidget.h"
#include "VB/VB.h"

UVBPauseMenuWidget::UVBPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 메뉴 입력만 받고, 게임을 멈추고, 뒤로 가기로 닫힌다.
	InputMode = EVBScreenInputMode::Menu;
	bPausesGame = true;
	bIsBackHandler = true;
}

void UVBPauseMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// BP 가 바를 안 넣었으면 아무 일도 하지 않는다(BindWidgetOptional 계약).
	if (!ReputationBar)
	{
		return;
	}

	// ASC 는 PlayerState 에 있다. 구체 클래스 캐스트 대신 인터페이스 경로로 묻는다.
	const AVBPlayerController* PC = GetVBOwningController();
	UAbilitySystemComponent* ASC = PC
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PC->PlayerState)
		: nullptr;

	if (!ASC)
	{
		// 바를 배치했는데 못 채운 것은 결함이라 경고를 남긴다.
		VB_LOG(Warning, "%s: 명성 바를 배치했으나 ASC 를 얻지 못했다 -> 바가 빈 채로 남는다. "
			"PlayerState 가 IAbilitySystemInterface 를 답하는지 확인하라", *GetName());
		return;
	}

	ReputationBar->InitializeWithASC(ASC);
}

void UVBPauseMenuWidget::ShowSavePage()
{
	// 실제 저장은 슬롯을 고른 뒤 UVBSaveSlotListWidget::SelectSlot 이 한다(자동 선택은 조용한 덮어쓰기).
	ShowSlotPage(EVBSlotListMode::Save);
}

void UVBPauseMenuWidget::ResumeGame()
{
	// 화면을 끄면 정지 해제와 입력 복귀는 UI 층이 한다.
	DeactivateWidget();
}

void UVBPauseMenuWidget::OpenCollection()
{
	if (AVBPlayerController* PC = GetVBOwningController())
	{
		PC->RequestCollectionScreen(FGameplayTag());
	}
}

void UVBPauseMenuWidget::OpenTutorial()
{
	if (AVBPlayerController* PC = GetVBOwningController())
	{
		PC->RequestTutorialScreen();
	}
}

void UVBPauseMenuWidget::QuitToMenu()
{
	if (AVBPlayerController* PC = GetVBOwningController())
	{
		PC->QuitToMainMenu();
	}
}

AVBPlayerController* UVBPauseMenuWidget::GetVBOwningController() const
{
	return Cast<AVBPlayerController>(GetOwningPlayer());
}

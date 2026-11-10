// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBSaveSlotListWidget.h"

#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Player/VBMenuPlayerController.h"
#include "UI/VBSaveSlotEntryWidget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBSaveSlotList"

UVBSaveSlotListWidget::UVBSaveSlotListWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 안전 기본값. WBP 가 덮어쓰면 그쪽이 이긴다.
	ModeTitles.Add(EVBSlotListMode::NewGame, LOCTEXT("NewGameTitle", "\uc0c8 \uac8c\uc784 - \uc2ac\ub86f \uc120\ud0dd"));
	ModeTitles.Add(EVBSlotListMode::Load, LOCTEXT("LoadTitle", "\ubd88\ub7ec\uc624\uae30"));
	ModeTitles.Add(EVBSlotListMode::Save, LOCTEXT("SaveTitle", "\uc800\uc7a5 - \uc2ac\ub86f \uc120\ud0dd"));
}

void UVBSaveSlotListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshTitle();
	RebuildEntries();
}

void UVBSaveSlotListWidget::SetListMode(EVBSlotListMode InMode)
{
	ListMode = InMode;
	RefreshTitle();

	// 재사용되면 NativeConstruct 가 다시 안 도므로 여기서 목록을 다시 만든다.
	RebuildEntries();
}

void UVBSaveSlotListWidget::RefreshTitle()
{
	if (!TxtTitle)
	{
		return;
	}

	// 문구가 없으면 경고한다.
	if (const FText* Title = ModeTitles.Find(ListMode))
	{
		TxtTitle->SetText(*Title);
		return;
	}

	VB_LOG(Warning, "SaveSlotList: 모드 %d 의 제목이 ModeTitles 에 없다 - WBP 기본값 확인", static_cast<int32>(ListMode));
	TxtTitle->SetText(FText::GetEmpty());
}

void UVBSaveSlotListWidget::RebuildEntries()
{
	if (!EntryBox)
	{
		VB_LOG(Warning, "SaveSlotList: EntryBox 미바인딩 - 목록을 그릴 자리가 없다. WBP 위젯 이름 확인");
		return;
	}

	if (!EntryWidgetClass)
	{
		VB_LOG(Warning, "SaveSlotList: EntryWidgetClass 미지정 - 항목을 만들 수 없다. WBP 기본값 확인");
		return;
	}

	EntryBox->ClearChildren();

	for (const FVBSaveSlotMeta& Meta : RefreshSlots())
	{
		UVBSaveSlotEntryWidget* Entry = CreateWidget<UVBSaveSlotEntryWidget>(this, EntryWidgetClass);
		if (!Entry)
		{
			continue;
		}
		// SetupFromMeta 안에서 나가는 요청도 받도록 구독을 먼저 건다.
		Entry->OnSelectRequested.AddDynamic(this, &UVBSaveSlotListWidget::HandleEntrySelect);
		Entry->OnDeleteRequested.AddDynamic(this, &UVBSaveSlotListWidget::HandleEntryDelete);
		Entry->SetupFromMeta(Meta);

		// 간격은 VerticalBox 슬롯에만 걸린다. 아니면 한 번 경고한다.
		UPanelSlot* AddedSlot = EntryBox->AddChild(Entry);
		if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(AddedSlot))
		{
			BoxSlot->SetPadding(EntrySpacing);
		}
		else if (!bEntrySpacingWarned)
		{
			bEntrySpacingWarned = true;
			VB_LOG(Warning, "SaveSlotList: EntryBox 가 VerticalBox 가 아니라 EntrySpacing 이 적용되지 않는다");
		}
	}
}

void UVBSaveSlotListWidget::HandleEntrySelect(int32 SlotIndex)
{
	SelectSlot(SlotIndex);
}

void UVBSaveSlotListWidget::HandleEntryDelete(int32 SlotIndex)
{
	DeleteSlot(SlotIndex);
}

TArray<FVBSaveSlotMeta> UVBSaveSlotListWidget::RefreshSlots() const
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		return Subsystem->GetAllSlotMetadata();
	}

	return {};
}

void UVBSaveSlotListWidget::SelectSlot(int32 SlotIndex)
{
	UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}

	switch (ListMode)
	{
	case EVBSlotListMode::NewGame:
		// 난이도 고르기를 이 슬롯과 함께 연다. 슬롯 점유는 그 화면의 확인 버튼이 한다.
		if (AVBMenuPlayerController* MenuPC = Cast<AVBMenuPlayerController>(GetOwningPlayer()))
		{
			MenuPC->RequestNewGame(SlotIndex);
			return;
		}
		// 새 게임 모드는 메뉴 레벨에서만 열린다. 난이도를 건너뛴 시작으로 덮지 않는다.
		VB_LOG(Error, "슬롯 목록: 주인이 메뉴 PC 가 아니다 - 슬롯 %d 에 새 게임을 시작하지 않는다", SlotIndex);
		return;

	case EVBSlotListMode::Save:
		// 구독을 먼저 건다. 엔진 AsyncSaveGameToSlot 은 실패 통지를 함수 안에서 바로 보낼 수 있다.
		// 요청이 거절되면 통지가 없으므로 앞 대기로 되돌린다(대기는 한 칸, 마지막 요청이 이긴다).
		{
			const int32 PreviousPending = PendingSaveSlot;
			PendingSaveSlot = SlotIndex;
			Subsystem->OnGameSaveCompleted.AddUniqueDynamic(this, &UVBSaveSlotListWidget::HandleSaveCompleted);
			if (!Subsystem->SaveCurrentGameToSlot(GetOwningPlayerPawn(), SlotIndex))
			{
				PendingSaveSlot = PreviousPending;
				if (PendingSaveSlot == INDEX_NONE)
				{
					Subsystem->OnGameSaveCompleted.RemoveDynamic(this, &UVBSaveSlotListWidget::HandleSaveCompleted);
				}
			}
		}

		return;

	case EVBSlotListMode::Load:
		// 빈 슬롯은 무시. 로드 완료 후 레벨 오픈.
		if (!Subsystem->DoesGameSaveExist(SlotIndex))
		{
			return;
		}

		Subsystem->OnGameLoadCompleted.AddDynamic(this, &UVBSaveSlotListWidget::HandleLoadCompleted);
		Subsystem->LoadGameSlot(SlotIndex);
		return;
	}
}

void UVBSaveSlotListWidget::DeleteSlot(int32 SlotIndex)
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->DeleteGameSave(SlotIndex);
		RebuildEntries(); // 지운 결과를 즉시 반영
		OnSlotsChanged(); // BP 가 덧붙일 갱신이 있으면
	}
}

void UVBSaveSlotListWidget::HandleSaveCompleted(int32 SlotIndex, bool bSuccess)
{
	// 다른 길이 쓴 저장 완료는 무시하고 계속 기다린다.
	if (SlotIndex != PendingSaveSlot)
	{
		return;
	}

	PendingSaveSlot = INDEX_NONE;
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->OnGameSaveCompleted.RemoveDynamic(this, &UVBSaveSlotListWidget::HandleSaveCompleted);
	}

	if (!bSuccess)
	{
		VB_LOG(Warning, "SaveSlotList: 슬롯 %d 저장 실패 - 목록을 갱신하지 않는다", SlotIndex);
		return;
	}

	// 방금 쓴 타임스탬프를 목록에 반영한다.
	RebuildEntries();
	OnSlotsChanged();
}

void UVBSaveSlotListWidget::HandleLoadCompleted(int32 SlotIndex, bool bSuccess)
{
	UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->OnGameLoadCompleted.RemoveDynamic(this, &UVBSaveSlotListWidget::HandleLoadCompleted);
	}

	if (bSuccess && Subsystem)
	{
		Subsystem->TravelToGameplayLevel();
	}
}

#undef LOCTEXT_NAMESPACE

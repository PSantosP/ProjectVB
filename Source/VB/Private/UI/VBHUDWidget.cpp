// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "UI/VBHUDWidget.h"

#include "Character/VBCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Data/VBHUDLayoutData.h"
#include "Engine/World.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "UI/Slots/VBHUDLayoutRuntime.h"
#include "UI/VBUIManagerSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Input/CommonUIInputTypes.h"
#include "Player/VBPlayerController.h"
#include "VB/VB.h"

UVBHUDWidget::UVBHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 게임 입력. 첫 클릭도 게임으로 보낸다 - 아니면 메뉴를 닫은 직후 첫 공격을 캡처가 먹는다.
	InputMode = EVBScreenInputMode::Game;
	GameMouseCaptureMode = EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
}

void UVBHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Pawn 초기화 전에도 안 보이게 처음엔 숨긴다.
	if (LockOnMarker)
	{
		LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 바인딩 기본 모드는 Menu 라 Game 모드에서 무시된다. HUD 바인딩은 모두 InputMode = Game 으로 건다.
	if (PauseInputAction)
	{
		FBindUIActionArgs PauseArgs(PauseInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandlePauseInput));
		PauseArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(PauseArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: PauseInputAction 미할당 - Esc 로 일시정지가 안 열린다 (WBP_HUD 클래스 기본값)");
	}

	if (InventoryInputAction)
	{
		FBindUIActionArgs InventoryArgs(InventoryInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandleInventoryInput));
		InventoryArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(InventoryArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: InventoryInputAction 미할당 - 인벤토리가 안 열린다 (WBP_HUD 클래스 기본값)");
	}

	if (JournalInputAction)
	{
		FBindUIActionArgs JournalArgs(JournalInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandleJournalInput));
		JournalArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(JournalArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: JournalInputAction 미할당 - 퀘스트 일지가 안 열린다 (WBP_HUD 클래스 기본값)");
	}

	if (SkillTreeInputAction)
	{
		FBindUIActionArgs SkillTreeArgs(SkillTreeInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandleSkillTreeInput));
		SkillTreeArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(SkillTreeArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: SkillTreeInputAction 미할당 - 스킬 트리가 안 열린다 (WBP_HUD 클래스 기본값)");
	}
}

void UVBHUDWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	// 줄은 한 번만 만든다. 구독은 줄마다 스스로 한다.
	TryInitializeFromOwningPawn();
}

void UVBHUDWidget::NativeDestruct()
{
	if (PresentationCanvas)
	{
		PresentationCanvas->RemoveFromParent();
	}
	StopWaitingForPawn();
	Super::NativeDestruct();
}

void UVBHUDWidget::StopWaitingForPawn()
{
	if (APlayerController* PC = PawnSource.Get())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBHUDWidget::HandlePossessedPawnChanged);
	}

	PawnSource.Reset();
}

void UVBHUDWidget::TryInitializeFromOwningPawn()
{
	if (bHUDInitialized)
	{
		return;
	}

	if (GetOwningPlayerPawn<AVBCharacter>())
	{
		InitializeHUD();
		return;
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PawnSource = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UVBHUDWidget::HandlePossessedPawnChanged);
	}
}

void UVBHUDWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (Cast<AVBCharacter>(NewPawn))
	{
		StopWaitingForPawn();
		if (!bHUDInitialized)
		{
			InitializeHUD();
		}
	}
}

void UVBHUDWidget::HandlePauseInput() const
{
	// 열어도 되는지(사망 연출 중이면 안 된다)는 PC 가 안다. HUD 는 요청만 한다.
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestPauseMenu();
	}
}

void UVBHUDWidget::HandleInventoryInput() const
{
	// 일시정지와 같다 - 열어도 되는지는 PC 가 안다.
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestInventoryScreen();
	}
}

void UVBHUDWidget::HandleJournalInput() const
{
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestJournalScreen();
	}
}

void UVBHUDWidget::HandleSkillTreeInput() const
{
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestSkillTreeScreen();
	}
}

bool UVBHUDWidget::TryHandleScreenAction(const UInputAction* Action) const
{
	if (!Action)
	{
		return false;
	}

	if (Action == PauseInputAction)
	{
		HandlePauseInput();
		return true;
	}

	if (Action == InventoryInputAction)
	{
		HandleInventoryInput();
		return true;
	}

	if (Action == JournalInputAction)
	{
		HandleJournalInput();
		return true;
	}

	if (Action == SkillTreeInputAction)
	{
		HandleSkillTreeInput();
		return true;
	}

	return false;
}

void UVBHUDWidget::InitializeHUD()
{
	bHUDInitialized = true;

	// 런타임 줄은 전부 배치 데이터가 정한다.
	BuildSlotsFromLayout();

	if (HUDFadeIn)
	{
		PlayAnimation(HUDFadeIn);
	}
}

void UVBHUDWidget::BuildSlotsFromLayout()
{
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!HUDLayout || !Canvas)
	{
		VB_LOG(Warning, "HUD: HUDLayout 또는 루트 CanvasPanel 이 없다");
		return;
	}
	VBHUDLayout::Build(*this, *Canvas, *HUDLayout, SlotWidgets, DefaultSlotHandles);
	if (!PresentationLayout)
	{
		return;
	}
	TArray<FString> Problems;
	PresentationLayout->CollectProblems(Problems);
	if (!Problems.IsEmpty() || PresentationLayout->Slots.IsEmpty() || PresentationLayout->Widgets.IsEmpty())
	{
		VB_LOG(Error, "HUD: 지속 표시 배치가 유효하지 않다: %s", *FString::Join(Problems, TEXT("; ")));
		return;
	}
	PresentationCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDPresentation"));
	VBHUDLayout::Build(*this, *PresentationCanvas, *PresentationLayout, SlotWidgets, DefaultSlotHandles);
	AttachPresentation();
}

void UVBHUDWidget::AttachPresentation()
{
	if (!PresentationCanvas)
	{
		return;
	}
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this);
	if (!UI || !UI->AttachPresentation(*PresentationCanvas))
	{
		VB_LOG(Error, "HUD: 지속 표시 영역을 UI 루트에 붙이지 못했다");
	}
}

void UVBHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	AttachPresentation();
}

void UVBHUDWidget::BeginDestroy()
{
	// 버려질 때만 기본 위젯 등록을 지운다(재구성 때는 다시 쓴다).
	//   GC 중이라 자리에는 알리지 않는다 - 위젯을 해체, 생성하면 안 된다.
	for (FVBUISlotHandle& Handle : DefaultSlotHandles)
	{
		Handle.ForgetWithoutNotify();
	}

	DefaultSlotHandles.Reset();
	Super::BeginDestroy();
}

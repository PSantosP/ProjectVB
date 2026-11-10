// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBHealthBarWidget.h"
#include "VBLockOnMarkerWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/Slots/VBUISlotSubsystem.h"   // FVBUISlotHandle - 기본 위젯 손잡이를 값으로 든다
#include "UI/VBScreenWidget.h"
#include "VBHUDWidget.generated.h"

class APlayerState;
class UInputAction;

/**
 * 게임 중 HUD. Game 층의 바닥 화면이다.
 * 꺼지지 않고 Game 입력을 말해야 메뉴를 닫은 뒤 조작이 돌아온다(그래서 사망 때도 층만 감춘다).
 */
UCLASS()
class VB_API UVBHUDWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBHUDWidget(const FObjectInitializer& ObjectInitializer);

	virtual void BeginDestroy() override;

	// 터치 버튼의 액션이 화면 입력이면 같은 Handle*Input 으로 보내고 참(주입으로는 CommonUI 바인딩이 안 불린다).
	bool TryHandleScreenAction(const UInputAction* Action) const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeOnActivated() override;
	virtual void NativeDestruct() override;

	// 일시정지를 여는 입력(IA_Pause). CommonUI 바인딩으로 받는다(CommonInputMetadata 를 붙이지 않는다).
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TObjectPtr<UInputAction> PauseInputAction;

	// 인벤토리를 여는 입력(IA_Inventory).
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TObjectPtr<UInputAction> InventoryInputAction;

	// 퀘스트 일지를 여는 입력(IA_Journal).
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TObjectPtr<UInputAction> JournalInputAction;

	// 스킬 트리를 여는 입력(IA_SkillTree).
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TObjectPtr<UInputAction> SkillTreeInputAction;

	// HUD 배치(DA_HUDLayout). 자리마다 UI 슬롯을 런타임에 만들고 기본 위젯을 등록한다. 비면 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TObjectPtr<class UVBHUDLayoutData> HUDLayout;

	// 자막 배치는 게임 HUD 와 별개인 UI 루트 영역에 붙인다.
	UPROPERTY(EditDefaultsOnly, Category="VB|HUD")
	TObjectPtr<class UVBHUDLayoutData> PresentationLayout;

	// 플레이어 체력 바. 스스로 PlayerState ASC 에 묶는다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UVBHealthBarWidget> PlayerHealthBar;
	
	// 기존 WBP_HUD 자식과의 호환용. 락온 이미지는 표시하지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UVBLockOnMarkerWidget> LockOnMarker;

private:
	// 조종 캐릭터가 생기면 한 번 초기화한다. 아직 없으면 PC 의 빙의 알림을 기다린다.
	void TryInitializeFromOwningPawn();
	void InitializeHUD();
	void StopWaitingForPawn();
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	bool bHUDInitialized = false;

	// 빙의 알림을 구독한 PC. 풀 때 다시 찾지 않고 여기서 푼다.
	TWeakObjectPtr<APlayerController> PawnSource;

	// 열기 요청만 한다. 열어도 되는지는 PC 가 정한다.
	void HandlePauseInput() const;
	void HandleInventoryInput() const;
	void HandleJournalInput() const;
	void HandleSkillTreeInput() const;

	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> HUDFadeIn;

	// 배치 데이터의 자리를 만들어 붙이고 기본 위젯을 등록한다(InitializeHUD 에서 한 번).
	void BuildSlotsFromLayout();
	void AttachPresentation();
	UPROPERTY(Transient)
	TObjectPtr<class UCanvasPanel> PresentationCanvas;

	// 이 HUD 가 만든 슬롯 자리들. Slate 재구성에도 같은 자리가 다시 붙는다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UVBUISlotWidget>> SlotWidgets;

	// 등록한 기본 위젯 손잡이. 한 번 등록하고 BeginDestroy 에서 지운다(재등록하면 항목 Id 가 바뀐다).
	TArray<FVBUISlotHandle> DefaultSlotHandles;
};

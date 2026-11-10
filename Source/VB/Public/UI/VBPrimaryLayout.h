// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "VBPrimaryLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetContainerBase;
class UCommonActivatableWidgetStack;
class UVBScreenWidget;
class UWidget;
class UOverlay;

/** LocalPlayer 마다 하나인 루트. Game/GameMenu/Menu/Modal/Dialog 스택을 담는다.
 * 트리가 비면 다섯을 만들고, WBP 파생은 GameLayer..DialogLayer 를 이름으로 줘야 한다.
 */
UCLASS()
class VB_API UVBPrimaryLayout : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	// 게임 층과 별개인 수동 표시 영역에 위젯을 붙인다.
	bool AttachPresentation(UWidget& Widget);
	bool HasPresentation() const;
	UCommonActivatableWidgetContainerBase* GetLayer(FGameplayTag LayerTag) const;

	// 층에 화면을 올린다. 층이 없으면 경고 후 null.
	UVBScreenWidget* PushToLayer(FGameplayTag LayerTag, TSubclassOf<UVBScreenWidget> ScreenClass);

	// 이 클래스의 화면이 어느 층에든 떠 있으면 그것을 돌려준다.
	UCommonActivatableWidget* FindScreen(TSubclassOf<UVBScreenWidget> ScreenClass) const;

	// 어느 층에 있든 그 화면을 내린다. 찾았으면 참.
	bool RemoveScreen(UCommonActivatableWidget& Screen);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>> Layers;

	// 트리가 비었으면 Overlay + 스택 다섯을 만든다. 순서가 곧 그리는 순서다(뒤에 넣은 층이 위).
	void BuildDefaultTreeIfEmpty();
	void BuildPresentationRoot();
	UPROPERTY(Transient)
	TObjectPtr<UOverlay> PresentationRoot;
	UPROPERTY(Transient)
	TObjectPtr<UOverlay> InteractiveRoot;
	void RegisterLayer(FGameplayTag LayerTag, FName LayerWidgetName);
	void HandleLayerTransitioning(UCommonActivatableWidgetContainerBase* Layer, bool bIsTransitioning);
	FName MakeTransitionReason(const UCommonActivatableWidgetContainerBase* Layer) const;
};

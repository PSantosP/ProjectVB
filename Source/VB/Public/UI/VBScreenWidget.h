// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Engine/EngineBaseTypes.h"   // EMouseCaptureMode
#include "VBScreenWidget.generated.h"

class UButton;

/** 이 화면이 맨 위일 때 입력이 가는 곳(Lyra 와 같은 값). Default = 아래 화면 설정 유지. */
UENUM(BlueprintType)
enum class EVBScreenInputMode : uint8
{
	Default,
	Game,
	GameAndMenu,
	Menu,
};

/**
 * 모든 화면의 부모. 입력, 정지는 원하는 값만 말하고 CommonUI 라우터와 UI 관리자가 적용한다(직접 SetInputMode 금지).
 * 컨테이너가 위젯을 재사용하므로 표시 값은 NativeOnActivated 에서 매번 채운다.
 */
UCLASS(Abstract, Blueprintable)
class VB_API UVBScreenWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	bool PausesGame() const { return bPausesGame; }

protected:
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Screen")
	EVBScreenInputMode InputMode = EVBScreenInputMode::Default;

	// Game, GameAndMenu 일 때 마우스 캡처 방식. Menu 는 잡지 않는다(잡으면 엔진이 게임 입력을 통과시킨다).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Screen",
		meta=(EditCondition="InputMode == EVBScreenInputMode::Game || InputMode == EVBScreenInputMode::GameAndMenu"))
	EMouseCaptureMode GameMouseCaptureMode = EMouseCaptureMode::CapturePermanently;

	// 활성인 동안 게임 정지를 원하나. 적용은 UI 관리자가 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Screen")
	bool bPausesGame = false;

	// 켜질 때 게임패드 초점을 받을 위젯 이름. 비면 BP 의 Get Desired Focus Target 에 맡긴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Screen")
	FName DefaultFocusWidgetName;

	// 닫기 버튼. 기본은 뒤로 가기와 같다. 터치에는 뒤로 가기 키가 없어 필요하다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnClose;

	// 닫기 버튼을 눌렀을 때. 뒤로 가기와 뜻이 다른 화면만 재정의한다.
	virtual void NativeOnCloseClicked() { HandleBackAction(); }

	// 참이면 BtnClose 가 없거나 뒤로 가기를 안 받을 때 WBP 컴파일이 실패한다(닫을 길 없는 화면 방지).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Screen")
	bool bRequireCloseButton = false;

	virtual void NativeOnInitialized() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	// 초점 대상을 네이티브로 정하는가(컴파일 검사용). NativeGetDesiredFocusTarget 을 재정의한 파생도 참을 돌려준다.
	virtual bool ProvidesNativeFocusTarget() const { return !DefaultFocusWidgetName.IsNone(); }

#if WITH_EDITOR
	// 메뉴 입력 화면이 초점 대상을 안 정하면 컴파일 때 경고한다(게임패드로 못 고르므로).
	virtual void ValidateCompiledWidgetTree(const UWidgetTree& BlueprintWidgetTree, class IWidgetCompilerLog& CompileLog) const override;
#endif

private:
	UFUNCTION()
	void HandleCloseClicked();
};

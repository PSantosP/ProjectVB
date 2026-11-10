// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBMenuScreenWidget.h"
#include "VBPauseMenuWidget.generated.h"

class AVBPlayerController;

/**
 * 게임 중 일시정지 메뉴. BP 버튼이 이 핸들러를 부르고, 레벨 전환은 소유 PC 에 위임한다.
 * 정지와 입력 모드는 UI 층이 맡는다(이 화면은 생성자에서 원하는 값만 말한다).
 */
UCLASS()
class VB_API UVBPauseMenuWidget : public UVBMenuScreenWidget
{
	GENERATED_BODY()

public:
	UVBPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	// 저장 화면으로 전환. 덮어쓸 슬롯이 보이도록 반드시 고르게 한다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ShowSavePage();

	// 게임 재개. 화면을 끄면 정지 해제와 입력 복귀는 UI 층이 한다. 뒤로 가기도 같은 길이다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ResumeGame();

	// 메인 메뉴로 복귀. 소유 PC 로 위임(unpause + OpenLevel).
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void QuitToMenu();

	// 도감 화면을 연다. 소유 PC 로 위임.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void OpenCollection();

	// 튜토리얼 다시 보기 화면을 연다. 소유 PC 로 위임.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void OpenTutorial();

protected:
	// 열 때마다 명성 바에 ASC 를 물려준다(화면 위젯은 재사용된다).
	virtual void NativeOnActivated() override;

	// 소유 PlayerController 를 VB 타입으로 캐스트.
	AVBPlayerController* GetVBOwningController() const;

	// 명성 바(선택). 없으면 표시만 빠진다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UVBReputationBarWidget> ReputationBar;
};

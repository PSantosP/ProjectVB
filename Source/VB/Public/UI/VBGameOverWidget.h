// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "VBGameOverWidget.generated.h"

class AVBPlayerController;

/**
 * 사망 시 뜨는 게임오버 화면. 두 버튼 모두 레벨 전환이라 소유 PC 에 위임한다.
 */
UCLASS()
class VB_API UVBGameOverWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBGameOverWidget(const FObjectInitializer& ObjectInitializer);

	// 계속하기 - 마지막 세이브로 재진입. 세이브가 없으면 PC 가 현재 레벨을 재시작한다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ContinueFromLastSave();

	// 메뉴로 가기 - 소유 PC 로 위임.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void QuitToMenu();

protected:
	// 켜질 때 버튼을 감췄다가 지연 후 드러낸다(문구를 먼저 읽히고 오조작을 막는다).
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

	// 뒤로 가기를 삼킨다. 두 버튼 말고는 나갈 길이 없어야 한다.
	virtual bool NativeOnHandleBackAction() override { return true; }

	// 소유 PlayerController 를 VB 타입으로 캐스트. 실패 시 nullptr.
	AVBPlayerController* GetVBOwningController() const;

	// 버튼 2종. 버튼이 없으면 빠져나갈 길이 없어 필수(BindWidget)다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> BtnContinue;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> BtnQuit;

	// 버튼이 나타나기까지의 지연(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float ButtonRevealDelay = 1.0f;

private:
	void RevealButtons();

	FTimerHandle ButtonRevealTimerHandle;
};

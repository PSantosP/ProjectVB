// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Settings/VBFrameRateRequest.h"
#include "VBMenuPlayerController.generated.h"

/**
 * 메인 메뉴 레벨 전용 PlayerController. 메뉴 화면을 여는 요청만 한다.
 * 화면 클래스는 UI.Screen.MainMenu 설정이, 입력 모드, 커서는 CommonUI 가 정한다.
 */
UCLASS()
class VB_API AVBMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 새 게임 난이도 고르기 화면을 연다. SlotIndex = INDEX_NONE 이면 슬롯 없이. 요청은 화면이 켜질 때 한 번 가져간다.
	// false = UI 관리자가 없어 못 연다(난이도를 건너뛴 시작으로 대신하지 않는다).
	bool RequestNewGame(int32 SlotIndex);
	// 고르기 화면이 켜질 때 한 번 가져간다(가져가면 비운다). 요청 없이 열렸으면 비어 있다.
	TOptional<int32> ConsumePendingNewGame();

	// 새 게임을 시작하는 유일한 길(고르기 화면 '확인'). 시작이 받아들여졌을 때만 게임 레벨로 간다.
	bool StartNewGameNow(int32 SlotIndex, FGameplayTag Difficulty);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TOptional<int32> PendingNewGameSlot;

	// 메뉴 프레임 제한을 실제로 받았는가(로컬 PC 만). EndPlay 가 이것으로 돌려준다.
	FVBFrameRateRequestHandle MenuFrameRateRequest;
};

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBMenuScreenWidget.h"
#include "VBMainMenuWidget.generated.h"

class UVBSaveGameSubsystem;

/** 메인 메뉴. BP 버튼이 이 핸들러를 부르고, C++ 가 세이브 서브시스템과 잇는다. */
UCLASS()
class VB_API UVBMainMenuWidget : public UVBMenuScreenWidget
{
	GENERATED_BODY()

public:
	UVBMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	// 저장된 게임이 하나라도 있는지 -> Continue/Load 버튼 활성화 판단.
	UFUNCTION(BlueprintPure, Category="VB|UI")
	bool HasAnySaveGame() const;

	// 가장 최근(타임스탬프 기준) 슬롯 인덱스. 없으면 -1.
	UFUNCTION(BlueprintPure, Category="VB|UI")
	int32 GetMostRecentSlotIndex() const;

	// Continue: 가장 최근 슬롯을 비동기 로드 -> 완료 시 게임 레벨 열기.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ContinueMostRecent();

	// New Game: 슬롯 없이 난이도 고르기를 연다(슬롯은 처음 저장할 때 정해진다). false = 열지 못함.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	bool StartNewGame();

	// 게임 종료.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void QuitGame();

	// 불러오기 화면으로 전환. 전환 규약은 UVBMenuScreenWidget 이 맡는다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void ShowLoadPage();

protected:
	virtual bool NativeOnHandleBackAction() override;

	// 로드 완료 콜백 - 성공 시 게임 레벨 오픈.
	UFUNCTION()
	void HandleLoadCompleted(int32 SlotIndex, bool bSuccess);

	// GameInstance 에서 세이브 서브시스템 조회.
	UVBSaveGameSubsystem* GetSaveSubsystem() const;
};

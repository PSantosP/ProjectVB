// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "Input/VBKeyBindingRules.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "VBKeyBindingScreenWidget.generated.h"

class IInputProcessor;
class UButton;
class UPanelWidget;
class UTextBlock;
class UVBInputUserSettings;
class UVBListRowWidget;

/**
 * 키 다시 지정 화면(Modal 층). 키 배치는 UVBInputUserSettings 가 관리하고 이 화면은 읽기, 바꾸기, 저장을 부른다.
 * 키 받기는 맨 앞 Slate 입력 전처리기가 하고 뗄 때 정한다. 상태: 고르기 / 키 대기 / 겹침 확인.
 */
UCLASS(Abstract)
class VB_API UVBKeyBindingScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBKeyBindingScreenWidget(const FObjectInitializer& ObjectInitializer);

	// 입력 전처리기가 뗀 키를 전달한다. 키를 기다리는 중이 아니면 무시한다.
	void HandleCapturedKey(const FKey& CapturedKey);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual bool NativeOnHandleBackAction() override;
	// 닫기는 키 대기 중이어도 화면을 닫는다(뒤로 가기는 대기를 먼저 거둔다).
	virtual void NativeOnCloseClicked() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 목록 한 줄의 WBP(동작 이름 + 지금 키). 비면 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;

	// 키를 기다릴 때 - 키보드 탭. 키보드에는 Esc 가 취소다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText WaitingKeyboardText;

	// 키를 기다릴 때 - 패드 탭. 취소 버튼이 없어 지금 버튼을 다시 누르면 그대로다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText WaitingGamepadText;

	// 받을 수 없는 키(다른 장치의 키, Esc, 시점 축 등, VBKeyBinding::CanBind). {Key} = 누른 키.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText CannotBindFormat;

	// 겹침 확인. {Key} = 새 키, {Actions} = 그 키를 쓰는 줄들의 이름.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText ConflictFormat;

	// 바꾼 뒤. {Action} = 줄 이름, {Key} = 새 키.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText ReboundFormat;

	// 바꾸기가 실패했을 때(까닭은 로그).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText RebindFailedText;

	// 기본값으로 되돌린 뒤.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText ResetDoneText;

	// 키가 없는 줄의 옆 글.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText UnboundKeyText;

	// 사용자 설정(UVBInputUserSettings)을 못 찾았을 때.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText NoSettingsText;

	// UMG 이름 바인딩. 목록, 상태, 탭은 필수라 이름이 틀리면 WBP 컴파일이 실패한다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtStatus;

	// 지금 탭의 버튼은 꺼 두어 지금 탭을 표시한다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnKeyboardTab;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnGamepadTab;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnReset;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBConfirmationScreensTestAccess;
	// 시험에서 사용자 설정 변경, 디스크 쓰기 없이 요청된 rebind 를 본다.
	TFunction<void(FName /*Mapping*/, EVBKeyDevice /*Device*/, const FKey& /*Key*/)> ApplyRebindForTests;
#endif
	enum class EState : uint8
	{
		Browsing,
		Waiting,
		Confirming,
	};

	UVBInputUserSettings* GetSettings() const;

	void ShowDevice(EVBKeyDevice Device);
	void Rebuild();
	void SelectRow(int32 RowIndex);
	void StartCapture();
	void StopCapture();
	void ApplyRebind(FName MappingName, const FKey& NewKey);
	void EnterBrowsing(const FText& Status);
	// 취소 전에 옛 질문을 무효로 해 재진입한 결과가 낡은 키를 적용하지 못하게 한다.
	void WithdrawConflict();
	void HandleConflictAnswer(EVBConfirmationResult Result);
	void SaveSettings();
	FText KeyText(const FKey& Key) const;
	FText RowName(FName MappingName) const;

	UFUNCTION()
	void HandleKeyboardTabClicked();

	UFUNCTION()
	void HandleGamepadTabClicked();

	UFUNCTION()
	void HandleResetClicked();

	EVBKeyDevice ShownDevice = EVBKeyDevice::KeyboardMouse;
	EState State = EState::Browsing;

	// 지금 그린 줄. 목록 줄의 번호 = 이 배열의 번호.
	TArray<FVBKeyBindingRow> Rows;

	// 입력을 기다리는 줄 번호. 초점 복원에만 쓴다.
	int32 EditingRow = INDEX_NONE;
	// 대기 중 매핑의 이름. 다시 그린 뒤 다른 줄을 가리킬 수 있는 번호가 아니다.
	FName PendingMapping;
	EVBKeyDevice PendingDevice = EVBKeyDevice::KeyboardMouse;
	FKey PendingKey;
	FVBConfirmationRequester ConflictAsk;

	// 키를 기다리는 동안만 Slate 에 끼워 둔다.
	TSharedPtr<IInputProcessor> CaptureProcessor;
};

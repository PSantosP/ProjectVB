// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CommonInputModeTypes.h"  // ECommonInputMode - 입력 모드 바뀜 인자
#include "CommonInputTypeEnum.h"   // ECommonInputType - 입력 종류 바뀜 인자
#include "InputActionValue.h"
#include "Widgets/Layout/Anchors.h"   // FAnchors - 배치 구조체가 값으로 든다
#include "VBTouchControls.generated.h"

class UInputAction;
class UInputModifier;
class UVBInputModifier_LookCalibration;
class UCommonInputSubsystem;
class UCommonUIActionRouterBase;
class UEnhancedInputLocalPlayerSubsystem;
class UVBTouchInputWidget;

// 화면 열기 액션을 주입 대신 처리한다(참 = 처리함). 슬롯 소유자(HUD)가 준다.
DECLARE_DELEGATE_RetVal_OneParam(bool, FVBTouchUIActionHandler, const UInputAction*);

// 화면 조작 하나의 배치. UVBTouchControlsWidget 이 목록으로 들고 런타임에 만든다.
USTRUCT(BlueprintType)
struct FVBTouchControlSlot
{
	GENERATED_BODY()

	// 스틱 / 시점 패드 / 버튼 중 하나의 WBP.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	TSubclassOf<UVBTouchInputWidget> WidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	TObjectPtr<UInputAction> InputAction;

	// 버튼 글자. 버튼이 아니면 무시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	FText Label;

	// 캔버스 슬롯 값(엔진 캔버스 규칙 그대로).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	FAnchors Anchors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	FVector2D Alignment = FVector2D(0.5f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	FVector2D Size = FVector2D(120.0f, 120.0f);

	// 겹칠 때 위에 오는 순서. 시점 패드는 버튼보다 낮아야 버튼이 눌린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	int32 ZOrder = 1;
};

/**
 * 화면 터치 조작의 공통 부모. 자기 위젯에서 시작한 손가락 하나를 붙잡고 입력 액션에 값을 주입한다.
 * 주입은 IMC 수식어를 거치지 않으므로 값은 수식어를 거친 뒤의 모양으로 넣는다.
 */
UCLASS(Abstract)
class VB_API UVBTouchInputWidget : public UUserWidget
{
	GENERATED_BODY()

	friend class FVBTouchTestAccess;

public:
	// UVBTouchControlsWidget 이 배치 목록대로 만들면서 넣는다.
	void SetInputAction(UInputAction* InAction) { InputAction = InAction; }

	// 손가락을 뗀 것으로 친다. 감추기만 하면 손 뗌 이벤트가 안 와서 주입이 계속된다.
	void ReleaseTouch() { EndActiveTouch(); }

protected:
	// 이 조작이 누르는 입력 액션. 배치 목록(FVBTouchControlSlot)이 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	TObjectPtr<UInputAction> InputAction;

	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void NativeDestruct() override;

	// 파생이 채운다. 좌표는 이 위젯의 로컬 좌표다.
	virtual void OnTouchBegin(const FVector2D& LocalPosition) {}
	virtual void OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta) {}
	virtual void OnTouchEnd() {}

	// 지금 손가락을 붙잡고 있나. OnTouchBegin 안에서 놓기가 끼어들었는지 볼 때 쓴다.
	bool HasActiveTouch() const { return ActivePointerIndex != INDEX_NONE; }

	// 누르는 동안 매 틱 같은 값을 넣는다. 멈추면 다음 틱에 Completed 가 난다.
	void StartInjection(const FInputActionValue& Value);
	void UpdateInjection(const FInputActionValue& Value);
	void StopInjection();

	// 이번 틱에 한 번만 넣는다(이동량 입력). 필요한 보정기는 직접 전달한다.
	void InjectOnce(const FInputActionValue& Value, const TArray<UInputModifier*>& Modifiers = {});

private:
	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem() const;

	// 지금 붙잡은 손가락. 없으면 INDEX_NONE.
	int32 ActivePointerIndex = INDEX_NONE;
	bool bInjecting = false;

	void EndActiveTouch();
};

/**
 * 가상 스틱. 위젯 중심 기준 오프셋/반경을 [-1,1] 로 계속 주입한다(이동).
 * 화면 위쪽 = +Y(앞). 게임패드 왼쪽 스틱과 같은 뜻이다.
 */
UCLASS()
class VB_API UVBTouchStick : public UVBTouchInputWidget
{
	GENERATED_BODY()

protected:
	// 끝까지 민 것으로 치는 거리(위젯 로컬 단위).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch", meta=(ClampMin="1.0"))
	float Radius = 100.0f;

	// 손가락을 따라 움직이는 손잡이 그림. 없어도 입력은 된다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UWidget> Thumb;

	virtual void OnTouchBegin(const FVector2D& LocalPosition) override;
	virtual void OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta) override;
	virtual void OnTouchEnd() override;

private:
	FVector2D ComputeValue(const FVector2D& LocalPosition) const;
	void PlaceThumb(const FVector2D& Value);
};

/** 시점 패드. 손가락 이동량(화면 px) x 배율을 그 틱에 한 번 넣는다. */
UCLASS()
class VB_API UVBTouchLookPad : public UVBTouchInputWidget
{
	GENERATED_BODY()

protected:
	// 축별 배율(부호 포함). 1 = 마우스 1픽셀 이동과 같은 양.
	// 화면 Y 가 이미 IA_Look 의 Negate Y 결과와 같아 둘 다 양수다. 그 IMC 매핑이 바뀌면 여기도 본다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	FVector2D LookScale = FVector2D(1.0f, 1.0f);

	virtual void OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta) override;

private:
	// 마우스와 같은 감도, Y반전 보정기. 주입 때 전달한다(처음 쓸 때 만든다).
	UPROPERTY(Transient)
	TObjectPtr<UVBInputModifier_LookCalibration> LookCalibration;
};

/** 버튼. 누르는 동안 true 를 계속 넣어 키보드 키처럼 Started/Completed 가 난다(홀드 능력도 된다). */
UCLASS()
class VB_API UVBTouchButton : public UVBTouchInputWidget
{
	GENERATED_BODY()

	friend class FVBTouchTestAccess;

public:
	void SetLabel(const FText& InLabel);
	void SetUIActionHandler(const FVBTouchUIActionHandler& InHandler) { UIActionHandler = InHandler; }

protected:
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> Label;

	// 누르는 동안의 불투명도. 뗄 때는 누르기 직전 값으로 돌린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PressedOpacity = 0.6f;

	virtual void OnTouchBegin(const FVector2D& LocalPosition) override;
	virtual void OnTouchEnd() override;

private:
	float ReleasedOpacity = 1.0f;

	// 화면 열기 액션의 처리기. 비어 있으면 늘 주입한다.
	FVBTouchUIActionHandler UIActionHandler;
};

/**
 * 화면 조작 묶음. Controls 목록대로 조작을 만들어 루트 CanvasPanel 에 붙인다.
 * 보임은 입력 종류, 입력 모드, vb.TouchControls 바뀜을 듣고 스스로 정한다(숨으면 틱이 안 돌아서).
 */
UCLASS()
class VB_API UVBTouchControlsWidget : public UUserWidget
{
	GENERATED_BODY()

	friend class FVBTouchTestAccess;

public:
	// 붙잡힌 손가락을 전부 놓는다. 숨기기 직전과 해체 때 부른다.
	void ReleaseAllTouches();

	// 0 = 마지막 입력이 터치일 때 / 1 = 늘 / 2 = 숨김. Menu 모드면 늘 숨긴다(주입은 Menu 입력 차단을 안 지난다).
	static bool ShouldShow(int32 Mode, ECommonInputMode InputMode, ECommonInputType InputType);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Touch")
	TArray<FVBTouchControlSlot> Controls;

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// 슬롯 소유자가 HUD 면 그 화면 액션 처리기를 버튼에 전달한다.
	void BindScreenActionHandler();
	void ApplyScreenActionHandler(const FVBTouchUIActionHandler& Handler);
	void UnbindSources();
	void HandleInputMethodChanged(ECommonInputType NewInputType);
	void HandleInputModeChanged(ECommonInputMode NewMode);
	void RefreshVisibility();

	// 만든 조작들. ReleaseAllTouches 가 돈다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBTouchInputWidget>> CreatedControls;

	// 구독한 출처. 해체 때 여기서 푼다.
	TWeakObjectPtr<UCommonInputSubsystem> InputSource;
	TWeakObjectPtr<UCommonUIActionRouterBase> ModeSource;

	ECommonInputType InputType = ECommonInputType::MouseAndKeyboard;
	// 라우터가 없으면 All(CommonUI 기본값과 같다).
	ECommonInputMode InputMode = ECommonInputMode::All;
	bool bWarnedNoScreenHandler = false;

	// 보임 계산 횟수. 시험이 구독 중복을 본다.
	int32 RefreshCount = 0;
};

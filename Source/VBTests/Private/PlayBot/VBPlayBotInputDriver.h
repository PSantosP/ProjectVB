// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "PlayBot/VBPlayBotClock.h"

class APlayerController;
class UInputAction;
class UInputMappingContext;

// FVBPlayBotPress - 예약된 버튼 누름 하나와 그 원인 기록 (telemetry 계약 v1, kind "in").
struct FVBPlayBotPress
{
	int32 Id = INDEX_NONE;
	FName Action; // UInputAction 속성 이름, 메뉴용 raw key 면 NAME_None
	FKey Key;
	FString Cue;
	int64 CueFrame = -1; // 게임에서 cue 가 일어난 dilation 없는 프레임
	int64 ObservedFrame = -1; // 사람이 볼 수 있던 프레임 (cue + 표시 지연)
	int64 PredictionFrame = -1; // 예측을 만든 프레임 (예측 누름)
	int64 PredictedCueFrame = -1; // 누름이 맞추는 예측된 미래 cue
	double DelayMs = 0.0;
	double ErrorMs = 0.0;
	int64 DueFrame = 0;
	int64 InjectedFrame = -1;
	int64 ReleasedFrame = -1;
	int32 HoldFrames = 1;
	bool bLate = false;
	FString Cancel;
	// NativeSeconds 모드에서만 쓰는 실제 초 단위 시각이다.
	double DueSeconds = -1.0;
	double HoldSeconds = 0.0;
	double InjectedSeconds = -1.0;
	double ReleasedSeconds = -1.0;
};

// FVBPlayBotInputDriver - 실제 패드와 같은 길(FSlateApplication -> CommonUI -> PlayerInput -> mapping context)로 가상 패드를 보낸다.
//   InjectInputForAction 으로 물러서지 않아 경로 실패가 숨지 않고 보고된다.
class FVBPlayBotInputDriver
{
public:
	explicit FVBPlayBotInputDriver(EVBPlayBotClockMode InMode = EVBPlayBotClockMode::FixedFrames) : Mode(InMode) {}
	TFunction<void(const FVBPlayBotPress& Press, bool bDown)> OnButton;

	// ResolveAction - 지금 걸린 context 에서 Action 에 묶인 패드 버튼을 찾는다. 패드 context 가 켜진 뒤 부른다.
	bool ResolveAction(APlayerController* PC, FName ActionName, const UInputAction* Action, FString& OutError);
	// ResolveActionInContext - Context 하나에서 Action 의 패드 버튼을, 없으면 키보드 키를 고른다. 메뉴가 열릴 때만 붙는 UI 맵(CommonUI 확인/뒤로)용.
	bool ResolveActionInContext(FName ActionName, const UInputAction* Action, const UInputMappingContext* Context, FString& OutError);
	bool ResolveGamepadAction(APlayerController* PC, FName ActionName, const UInputAction* Action, FString& OutError);
	bool ValidateStickActions(APlayerController* PC, const UInputAction* Move, const UInputAction* Look, FString& Error) const;
	bool HasAction(FName Action) const { return ButtonKeys.Contains(Action); }
	FKey GetKey(FName Action) const { return ButtonKeys.FindRef(Action); }
	// IsKeyboardFallback - 패드 버튼이 없어 키보드 키로 누른 Action 인지. 그 자체가 발견이다.
	bool IsKeyboardFallback(FName Action) const { return KeyboardFallback.Contains(Action); }
	FString DescribeKeys() const;

	// Schedule - id 를 돌려준다. key 도 해석된 action 도 없으면 INDEX_NONE.
	int32 Schedule(FVBPlayBotPress Press);
	bool IsActive(int32 Id) const;
	// CancelPending - 아직 주입 안 된 누름을 취소한다. 누르고 있는 것은 제때 뗀다.
	void CancelPending(const FString& Reason);
	void SetStickWant(const FVector2D& Want) { StickWant = Want; }
	// SnapStick - 사람 같지 않은 커버리지 도구. motor ramp 없이 다음 전송에 Want 에 있어 중립을 안 거친다.
	void SnapStick(const FVector2D& Want) { StickWant = Want; StickOut = Want; }
	void SetLookWant(const FVector2D& Want) { LookWant = Want; }
	void ReleaseAll(int64 Frame, double Seconds = -1.0);

	// SendWakeTap - CommonInput 을 패드로 바꾸는 작은 스틱 탭. 게임 의도는 없다.
	void SendWakeTap();

	// Tick - 월드가 입력을 읽기 전 매 프레임 한 번 부른다. Slate 가 없으면 false.
	bool Tick(int64 Frame, double FrameMs, double MotorRampMs);
	// 실제 초로 예약을 판단하고 엔진 프레임 번호는 기록에만 쓴다.
	bool TickNative(int64 EngineFrame, double Seconds, double DeltaMs, double MotorRampMs);

	const FVector2D& GetStickWant() const { return StickWant; }
	const FVector2D& GetStickOut() const { return StickOut; }
	const FVector2D& GetLookOut() const { return LookOut; }
	TArray<FString> GetDownKeys() const;

	static FString PathName() { return TEXT("slate_gamepad"); }

private:
	// PickKey - Pad 맵의 패드 버튼을 먼저, 없으면 Keyboard 맵의 키를 고르고 그 경로를 keyboard_fallback 으로 남긴다.
	bool PickKey(FName ActionName, const UInputAction* Action, const UInputMappingContext* Pad, const UInputMappingContext* Keyboard,
		FString& OutError);
	bool TickScheduled(int64 Frame, double FrameMs, double MotorRampMs);
	EVBPlayBotClockMode Mode;
	double NativeSeconds = -1.0;
	void SendButton(const FKey& Key, bool bDown) const;
	void SendAxis(const FKey& Key, float Value) const;
	// UpdateStickButtons - 실제 XInput 패드처럼 스틱 방향 가상 버튼을 보낸다.
	void UpdateStickButtons(const FVector2D& Stick, bool bRight);
	static FVector2D Ramp(const FVector2D& Out, const FVector2D& Want, double Step);
	void SendStick(const FVector2D& Out, FVector2D& Sent, const FKey& X, const FKey& Y);

	TMap<FName, FKey> ButtonKeys;
	TSet<FName> KeyboardFallback;
	TArray<FVBPlayBotPress> Pending;
	TArray<FVBPlayBotPress> Held;
	FVector2D StickWant = FVector2D::ZeroVector;
	FVector2D StickOut = FVector2D::ZeroVector;
	FVector2D StickSent = FVector2D(TNumericLimits<double>::Max());
	FVector2D LookWant = FVector2D::ZeroVector;
	FVector2D LookOut = FVector2D::ZeroVector;
	FVector2D LookSent = FVector2D(TNumericLimits<double>::Max());
	bool bStickButtons[8] = { false, false, false, false, false, false, false, false };
	int32 NextId = 1;
};

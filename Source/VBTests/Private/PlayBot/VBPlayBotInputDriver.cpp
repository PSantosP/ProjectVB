// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotInputDriver.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Input/VBInputDeviceManager.h"

namespace
{
	// Slate user 0 은 첫 로컬 플레이어의 패드다.
	constexpr uint32 PadUserIndex = 0;
	// XInput 은 dead zone(왼쪽 7849, 오른쪽 8689 / 32767)을 넘은 스틱 방향을 버튼으로도 알린다.
	//   CommonInput 은 button-down 에서만 패드로 바뀌므로 실제 패드도 이 버튼으로 바뀐다.
	constexpr double LeftStickButtonThreshold = 7849.0 / 32767.0;
	constexpr double RightStickButtonThreshold = 8689.0 / 32767.0;
}

bool FVBPlayBotInputDriver::ResolveAction(APlayerController* PC, FName ActionName, const UInputAction* Action, FString& OutError)
{
	if (ButtonKeys.Contains(ActionName))
	{
		return true;
	}

	ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input)
	{
		OutError = TEXT("no Enhanced Input subsystem on the local player");
		return false;
	}

	if (!Action)
	{
		OutError = FString::Printf(TEXT("input action %s is neither an input tag in the pawn's input config nor a controller slot (pawn actions are named by input tag, e.g. Input.Attack.Light)"), *ActionName.ToString());
		return false;
	}
	// CommonInput 이 활성 장치를 바꾸기 전에도 봇은 설정된 패드 경로를 고른다.
	const UVBInputDeviceManager* Devices = UVBInputDeviceManager::FindFor(PC->GetPawn());
	return PickKey(ActionName, Action, Devices ? Devices->GetMappingContext(ECommonInputType::Gamepad) : nullptr,
		Devices ? Devices->GetMappingContext(ECommonInputType::MouseAndKeyboard) : nullptr, OutError);
}

bool FVBPlayBotInputDriver::ResolveActionInContext(FName ActionName, const UInputAction* Action, const UInputMappingContext* Context,
	FString& OutError)
{
	if (ButtonKeys.Contains(ActionName))
	{
		return true;
	}

	if (!Action || !Context)
	{
		OutError = FString::Printf(TEXT("%s needs its input action and the mapping context that carries it"), *ActionName.ToString());
		return false;
	}

	return PickKey(ActionName, Action, Context, Context, OutError);
}

bool FVBPlayBotInputDriver::PickKey(FName ActionName, const UInputAction* Action, const UInputMappingContext* Pad,
	const UInputMappingContext* Keyboard, FString& OutError)
{
	// 축과 마우스 버튼은 누를 버튼이 아니다. bGamepad 면 패드 버튼만, 아니면 키보드 키만 고른다.
	auto FindKey = [Action](const UInputMappingContext* Context, bool bGamepad)
	{
		if (!Context)
		{
			return FKey();
		}

		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			const FKey& Key = Mapping.Key;
			if (Mapping.Action == Action && Key.IsValid() && !Key.IsAnalog() && Key.IsGamepadKey() == bGamepad
				&& (bGamepad || !Key.IsMouseButton()))
			{
				return Key;
			}
		}

		return FKey();
	};

	if (const FKey PadKey = FindKey(Pad, true); PadKey.IsValid())
	{
		ButtonKeys.Add(ActionName, PadKey);
		return true;
	}

	// 패드 버튼이 없으면 keyboard/mouse context 의 키를 쓰고 경로를 "keyboard_fallback" 으로 기록한다.
	if (const FKey BoardKey = FindKey(Keyboard, false); BoardKey.IsValid())
	{
		ButtonKeys.Add(ActionName, BoardKey);
		KeyboardFallback.Add(ActionName);
		return true;
	}

	OutError = FString::Printf(TEXT("no gamepad button or keyboard key is mapped to %s"), *ActionName.ToString());
	return false;
}

FString FVBPlayBotInputDriver::DescribeKeys() const
{
	TArray<FString> Parts;
	for (const TPair<FName, FKey>& Pair : ButtonKeys)
	{
		Parts.Add(FString::Printf(TEXT("\"%s\":\"%s\""), *Pair.Key.ToString(), *Pair.Value.ToString()));
	}

	return FString::Printf(TEXT("{%s}"), *FString::Join(Parts, TEXT(",")));
}

int32 FVBPlayBotInputDriver::Schedule(FVBPlayBotPress Press)
{
	const bool bNative = Mode == EVBPlayBotClockMode::NativeSeconds;
	if ((bNative && (!FMath::IsFinite(Press.DueSeconds) || Press.DueSeconds < 0.0
		|| !FMath::IsFinite(Press.HoldSeconds) || Press.HoldSeconds <= 0.0))
		|| (!bNative && (Press.DueSeconds >= 0.0 || Press.HoldSeconds != 0.0))) { return INDEX_NONE; }
	if (!Press.Key.IsValid())
	{
		Press.Key = ButtonKeys.FindRef(Press.Action);
	}

	if (!Press.Key.IsValid())
	{
		return INDEX_NONE;
	}

	Press.Id = NextId++;
	Pending.Add(MoveTemp(Press));
	return Pending.Last().Id;
}

bool FVBPlayBotInputDriver::IsActive(int32 Id) const
{
	auto Match = [Id](const FVBPlayBotPress& Press) { return Press.Id == Id; };
	return Pending.ContainsByPredicate(Match) || Held.ContainsByPredicate(Match);
}

void FVBPlayBotInputDriver::CancelPending(const FString& Reason)
{
	for (FVBPlayBotPress& Press : Pending)
	{
		Press.Cancel = Reason;
		if (OnButton)
		{
			OnButton(Press, false);
		}
	}

	Pending.Reset();
}

void FVBPlayBotInputDriver::ReleaseAll(int64 Frame, double Seconds)
{
	if (Mode == EVBPlayBotClockMode::NativeSeconds && FMath::IsFinite(Seconds) && Seconds >= NativeSeconds)
	{ NativeSeconds = Seconds; }
	CancelPending(TEXT("release_all"));
	for (FVBPlayBotPress& Press : Held)
	{
		SendButton(Press.Key, false);
		Press.ReleasedFrame = Frame;
		Press.ReleasedSeconds = NativeSeconds;
		if (OnButton)
		{
			OnButton(Press, false);
		}
	}

	Held.Reset();
	StickWant = StickOut = LookWant = LookOut = FVector2D::ZeroVector;
	SendAxis(EKeys::Gamepad_LeftX, 0.f);
	SendAxis(EKeys::Gamepad_LeftY, 0.f);
	SendAxis(EKeys::Gamepad_RightX, 0.f);
	SendAxis(EKeys::Gamepad_RightY, 0.f);
	UpdateStickButtons(FVector2D::ZeroVector, false);
	UpdateStickButtons(FVector2D::ZeroVector, true);
	StickSent = LookSent = FVector2D::ZeroVector;
}

void FVBPlayBotInputDriver::SendWakeTap()
{
	// CommonInput 은 패드 button down 으로 바꾼다. axis 값을 쓰면 PlayerInput 이 한 프레임 안에서 누적해 스틱이 남는다.
	SendButton(EKeys::Gamepad_LeftStick_Right, true);
	SendButton(EKeys::Gamepad_LeftStick_Right, false);
	// 다음 tick 에 중앙 스틱을 다시 보내 context 전환 뒤 옛 axis 값이 남지 않게 한다.
	StickSent = LookSent = FVector2D(TNumericLimits<double>::Max());
}

FVector2D FVBPlayBotInputDriver::Ramp(const FVector2D& Out, const FVector2D& Want, double Step)
{
	const FVector2D Delta = Want - Out;
	return Delta.Size() <= Step ? Want : Out + Delta.GetSafeNormal() * Step;
}

void FVBPlayBotInputDriver::SendStick(const FVector2D& Out, FVector2D& Sent, const FKey& X, const FKey& Y)
{
	// 패드는 기울어진 동안 매 프레임 axis 를 보내고, 중앙으로 오면 0 을 한 번 보낸다.
	if (!Out.IsNearlyZero() || !Sent.IsNearlyZero())
	{
		SendAxis(X, static_cast<float>(Out.X));
		SendAxis(Y, static_cast<float>(Out.Y));
		Sent = Out;
	}
}

bool FVBPlayBotInputDriver::Tick(int64 Frame, double FrameMs, double MotorRampMs)
{
	return Mode == EVBPlayBotClockMode::FixedFrames && TickScheduled(Frame, FrameMs, MotorRampMs);
}

bool FVBPlayBotInputDriver::TickNative(int64 EngineFrame, double Seconds, double DeltaMs, double MotorRampMs)
{
	if (Mode != EVBPlayBotClockMode::NativeSeconds || !FMath::IsFinite(Seconds) || Seconds < 0.0
		|| Seconds < NativeSeconds || !FMath::IsFinite(DeltaMs) || DeltaMs < 0.0) { return false; }
	NativeSeconds = Seconds;
	return TickScheduled(EngineFrame, DeltaMs, MotorRampMs);
}

bool FVBPlayBotInputDriver::TickScheduled(int64 Frame, double FrameMs, double MotorRampMs)
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	for (int32 Index = Held.Num() - 1; Index >= 0; --Index)
	{
		FVBPlayBotPress& Press = Held[Index];
		if (Mode == EVBPlayBotClockMode::NativeSeconds ? NativeSeconds >= Press.InjectedSeconds + Press.HoldSeconds
			: Frame >= Press.InjectedFrame + Press.HoldFrames)
		{
			SendButton(Press.Key, false);
			Press.ReleasedFrame = Frame;
			Press.ReleasedSeconds = NativeSeconds;
			if (OnButton)
			{
				OnButton(Press, false);
			}

			Held.RemoveAt(Index);
		}
	}

	for (int32 Index = 0; Index < Pending.Num();)
	{
		FVBPlayBotPress& Press = Pending[Index];
		if (Mode == EVBPlayBotClockMode::NativeSeconds ? NativeSeconds < Press.DueSeconds : Frame < Press.DueFrame)
		{
			++Index;
			continue;
		}
		// 이미 지난 예정 시각은 지금 실행하고 늦음 표시를 한다. 거슬러 기록하지 않는다.
		Press.bLate = Mode == EVBPlayBotClockMode::NativeSeconds ? NativeSeconds > Press.DueSeconds : Frame > Press.DueFrame;
		Press.InjectedSeconds = NativeSeconds;
		Press.InjectedFrame = Frame;
		SendButton(Press.Key, true);
		if (OnButton)
		{
			OnButton(Press, true);
		}

		Held.Add(MoveTemp(Press));
		Pending.RemoveAt(Index);
	}

	// Motor ramp - 엄지가 새 스틱 목표에 한 프레임이 아니라 MotorRampMs 에 걸쳐 닿는다.
	const double Step = MotorRampMs > 0.0 ? FrameMs / MotorRampMs : 1.0;
	StickOut = Ramp(StickOut, StickWant, Step);
	LookOut = Ramp(LookOut, LookWant, Step);
	SendStick(StickOut, StickSent, EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY);
	SendStick(LookOut, LookSent, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY);
	UpdateStickButtons(StickOut, false);
	UpdateStickButtons(LookOut, true);
	return true;
}

TArray<FString> FVBPlayBotInputDriver::GetDownKeys() const
{
	TArray<FString> Keys;
	for (const FVBPlayBotPress& Press : Held)
	{
		Keys.Add(Press.Key.ToString());
	}

	return Keys;
}

void FVBPlayBotInputDriver::UpdateStickButtons(const FVector2D& Stick, bool bRight)
{
	const double Threshold = bRight ? RightStickButtonThreshold : LeftStickButtonThreshold;
	const bool bWanted[4] = { Stick.Y > Threshold, Stick.Y < -Threshold, Stick.X < -Threshold, Stick.X > Threshold };
	static const FKey LeftKeys[4] = { EKeys::Gamepad_LeftStick_Up, EKeys::Gamepad_LeftStick_Down,
		EKeys::Gamepad_LeftStick_Left, EKeys::Gamepad_LeftStick_Right };
	static const FKey RightKeys[4] = { EKeys::Gamepad_RightStick_Up, EKeys::Gamepad_RightStick_Down,
		EKeys::Gamepad_RightStick_Left, EKeys::Gamepad_RightStick_Right };
	const FKey* Keys = bRight ? RightKeys : LeftKeys;
	bool* States = bStickButtons + (bRight ? 4 : 0);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (bWanted[Index] != States[Index])
		{
			SendButton(Keys[Index], bWanted[Index]);
			States[Index] = bWanted[Index];
		}
	}
}

void FVBPlayBotInputDriver::SendButton(const FKey& Key, bool bDown) const
{
	if (!FSlateApplication::IsInitialized() || !Key.IsValid())
	{
		return;
	}

	const FKeyEvent Event(Key, FModifierKeysState(), PadUserIndex, false, 0, 0);
	if (bDown)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(Event);
	}
	else
	{
		FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}
}

void FVBPlayBotInputDriver::SendAxis(const FKey& Key, float Value) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	FSlateApplication::Get().ProcessAnalogInputEvent(FAnalogInputEvent(Key, FModifierKeysState(), PadUserIndex, false, 0, 0, Value));
}


bool FVBPlayBotInputDriver::ValidateStickActions(APlayerController* PC, const UInputAction* Move, const UInputAction* Look, FString& Error) const
{
	const UVBInputDeviceManager* Devices = PC ? UVBInputDeviceManager::FindFor(PC->GetPawn()) : nullptr;
	const UInputMappingContext* Pad = Devices ? Devices->GetMappingContext(ECommonInputType::Gamepad) : nullptr;
	if (!Pad || !Move || !Look || Move == Look) { Error = TEXT("native route movement/look mapping owners are missing"); return false; }
	auto HasAxes = [Pad](const UInputAction* Action, FKey XY, FKey X, FKey Y)
	{
		bool bX = false, bY = false;
		for (const FEnhancedActionKeyMapping& Mapping : Pad->GetMappings())
		{
			if (Mapping.Action != Action) { continue; }
			if (Mapping.Key == XY) { return true; }
			bX |= Mapping.Key == X; bY |= Mapping.Key == Y;
		}
		return bX && bY;
	};
	if (!HasAxes(Move,EKeys::Gamepad_Left2D,EKeys::Gamepad_LeftX,EKeys::Gamepad_LeftY)
		|| !HasAxes(Look,EKeys::Gamepad_Right2D,EKeys::Gamepad_RightX,EKeys::Gamepad_RightY))
	{ Error = TEXT("native route requires the declared left-stick move and right-stick look mappings"); return false; }
	return true;
}


bool FVBPlayBotInputDriver::ResolveGamepadAction(APlayerController* PC, FName ActionName, const UInputAction* Action, FString& OutError)
{
	if (!ResolveAction(PC,ActionName,Action,OutError)) { return false; }
	if (!IsKeyboardFallback(ActionName)) { return true; }
	ButtonKeys.Remove(ActionName); KeyboardFallback.Remove(ActionName);
	OutError = FString::Printf(TEXT("native gamepad action %s resolved only to keyboard"),*ActionName.ToString());
	return false;
}

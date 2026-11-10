// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/** 키 줄의 장치. 키보드, 마우스 IMC 와 패드 IMC 가 따로다. */
enum class EVBKeyDevice : uint8
{
	KeyboardMouse,
	Gamepad,
};

/** 키 다시 지정 화면의 한 줄. */
struct FVBKeyBindingRow
{
	FName MappingName;
	EVBKeyDevice Device = EVBKeyDevice::KeyboardMouse;
	FKey Key;
	FKey DefaultKey;
	FText DisplayName;
	FText DisplayCategory;
};

/**
 * 키 다시 지정 판정. 엔진 MapPlayerKey 는 겹치는 키를 막지 않아 받을 수 있는 키와 겹침을 여기서 답한다.
 * 사용자 설정, 월드 없이 부를 수 있다.
 */
namespace VBKeyBinding
{
	// 키가 속한 장치. 패드 키면 패드, 그 밖은 키보드, 마우스.
	VB_API EVBKeyDevice DeviceOf(const FKey& Key);

	// 이 장치의 줄이 받을 수 있는 키인가. 빈 키, 다른 장치, 터치, 2D 축과 그 한 축, Esc 는 못 받는다.
	// 패드 트리거 축은 받는다.
	VB_API bool CanBind(const FKey& Key, EVBKeyDevice RowDevice);

	// 같은 물리 버튼을 한 키로 모은다. 패드 트리거는 버튼과 축 두 키라 축으로 모아야 겹침을 잡는다.
	VB_API FKey NormalizeKey(const FKey& Key);

	// NewKey 를 Editing 줄에 주면 겹치는 같은 장치의 다른 줄 이름들(Rows 순서, 중복 없음).
	// 두 줄의 기본 키가 모두 NewKey 면 기본 배치가 일부러 나눈 키라 겹침이 아니다.
	VB_API TArray<FName> FindConflicts(const TArray<FVBKeyBindingRow>& Rows, FName Editing, EVBKeyDevice Device, const FKey& NewKey);

	// 분류가 처음 나온 순서로 묶고, 묶음 안은 등록 순서를 지킨다. 분류는 표시 글자로 견준다.
	VB_API void OrderByCategory(TArray<FVBKeyBindingRow>& Rows);

	// 패드에서 질주(L3)를 누른 채 서서 앉기(R3)를 누르면 필살기다. 필살기에 패드 버튼을 따로 묶었으면 꺼진다.
	VB_API bool IsSkillChordPress(bool bGamepad, bool bSprintHeld, bool bMoveInput, bool bPadSkillKeyBound);
}

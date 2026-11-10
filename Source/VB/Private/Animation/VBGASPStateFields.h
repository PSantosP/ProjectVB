#pragma once

#include "CoreMinimal.h"
#include "VBGASPStateView.h"

class UAnimationAsset;

DECLARE_LOG_CATEGORY_EXTERN(LogVBGASPState, Log, All);

// 이동 그래프 변수를 이름으로 쓰고 읽는 함수 모음. 칸이 없거나 형이 다르면 쓰지 않고 false 를 돌려준다.
namespace VBGASP
{
	// 이름 배열 칸에 쓴다.
	bool WriteNames(const FStateView& View, FName Name, const TArray<FName>& Names);
	// bool 칸에 쓴다.
	bool WriteBool(const FStateView& State, FName Name, bool Value);
	// 숫자 칸에 쓴다(칸이 정수면 잘라서 쓴다).
	bool WriteNumber(const FStateView& State, FName Name, double Value);
	// 객체 칸에 쓴다(형이 맞을 때만).
	bool WriteObject(const FStateView& State, FName Name, UObject* Value);
	// 같은 형의 칸 하나를 다른 칸으로 복사한다.
	bool CopyField(const FStateView& Destination, FName DestinationName, const FStateView& Source, FName SourceName);
	// FName 칸을 읽는다. 없으면 NAME_None.
	FName ReadName(const FStateView& State, FName Name);
	// 그래프의 ValidAnims 배열을 통째로 바꾼다.
	bool StoreValidAnimations(const FStateView& State, const TArray<UAnimationAsset*>& Animations);
	// 그래프의 StateMachineState 값을 저작한 이름으로 읽는다.
	FName CMCStateName(const FStateView& View);
}

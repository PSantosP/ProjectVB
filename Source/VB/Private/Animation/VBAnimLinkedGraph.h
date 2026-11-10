// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

class FStructProperty;
class UAnimInstance;
class UPoseSearchDatabase;

// 연결된 GASP 그래프의 변수를 리플렉션으로 찾고 읽고 쓰는 함수 모음. 게임 스레드에서 부른다.
namespace VBAnimLinkedGraph
{
	// 그래프의 FTransform InteractionTransform 칸을 찾는다. 없으면 한 번 경고하고 null.
	FStructProperty* FindInteractionTransformField(const UAnimInstance* Linked);

	// 연결된 그래프 전부에 전이 노티파이 값을 세운다.
	void RaiseEarlyTransition(const UAnimInstance& Main, bool bToLoop);

	// 실제로 포즈를 고른 그래프의 선택 DB 와 태그를 읽는다.
	void ReadSelectedDatabase(UAnimInstance& Graph, TObjectPtr<const UPoseSearchDatabase>& OutDatabase, TArray<FName>& OutTags);
}

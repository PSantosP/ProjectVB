// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"

// 링크된 이동 그래프의 노드를 태그로 찾는 길 하나. 태그 이름은 그래프 자산이 든다.
namespace VBGASP
{
inline FName BlendStackTag()
{
	static const FName Tag(TEXT("State Machine Blend Stack"));
	return Tag;
}

inline const FAnimSubsystem_Tag* FindTagSubsystem(const UAnimInstance* Instance)
{
	const IAnimClassInterface* Class = Instance ? IAnimClassInterface::GetFromClass(Instance->GetClass()) : nullptr;
	return Class ? Class->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
}

inline FAnimNodeReference FindTaggedNode(UAnimInstance* Instance, FName Tag)
{
	const FAnimSubsystem_Tag* Tags = FindTagSubsystem(Instance);
	// FindNodeIndexByTag 는 FAnimNodeReference 가 쓰는 뒤집힌 프로퍼티 번호를 이미 돌려준다.
	return Tags ? FAnimNodeReference(Instance, Tags->FindNodeIndexByTag(Tag)) : FAnimNodeReference();
}

inline bool FindBlendStack(UAnimInstance* Instance, FBlendStackAnimNodeReference& Reference)
{
	EAnimNodeReferenceConversionResult Result;
	Reference = UBlendStackAnimNodeLibrary::ConvertToBlendStackNode(FindTaggedNode(Instance, BlendStackTag()), Result);
	return Result == EAnimNodeReferenceConversionResult::Succeeded;
}
}

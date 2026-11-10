// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBAnimLinkedGraph.h"

#include "Animation/AnimInstance.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "VB/VB.h"

namespace VBAnimLinkedGraph
{
	FStructProperty* FindInteractionTransformField(const UAnimInstance* Linked)
	{
		FStructProperty* Field = Linked ? FindFProperty<FStructProperty>(Linked->GetClass(),
			TEXT("InteractionTransform")) : nullptr;
		if (Field && Field->Struct != TBaseStructure<FTransform>::Get())
		{
			Field = nullptr;
		}

		if (Linked && !Field)
		{
			// 그래프가 바뀔 때 한 번 알린다 - 이 필드가 없으면 그래프의 트래버설 채널과 FromTraversal 블렌드가 렛지를 못 본다.
			VB_LOG(Warning, "[Traversal] Linked graph %s has no FTransform InteractionTransform - its traversal channels get no ledge",
				*GetNameSafe(Linked->GetClass()));
		}

		return Field;
	}

	void RaiseEarlyTransition(const UAnimInstance& Main, bool bToLoop)
	{
		for (const FName Tag : {FName(TEXT("GASPLocomotion")), FName(TEXT("GASPSlide")), FName(TEXT("GASPUnarmed"))})
		{
			if (UAnimInstance* Linked = Main.GetLinkedAnimGraphInstanceByTag(Tag))
			{
				const FName Flag = bToLoop ? TEXT("NotifyTransition_ToLoop") : TEXT("NotifyTransition_Re-Transition");
				if (FBoolProperty* Field = FindFProperty<FBoolProperty>(Linked->GetClass(), Flag))
				{
					Field->SetPropertyValue_InContainer(Linked, true);
				}
			}
		}
	}

	void ReadSelectedDatabase(UAnimInstance& Graph, TObjectPtr<const UPoseSearchDatabase>& OutDatabase, TArray<FName>& OutTags)
	{
		// 무기 chooser 입력을 실제로 포즈를 고른 그래프와 맞춘다.
		const FObjectPropertyBase* Selected = FindFProperty<FObjectPropertyBase>(
			Graph.GetClass(), TEXT("CurrentSelectedDatabase"));
		OutDatabase = Selected ? Cast<UPoseSearchDatabase>(
			Selected->GetObjectPropertyValue_InContainer(&Graph)) : nullptr;
		const FArrayProperty* SelectedTags = FindFProperty<FArrayProperty>(
			Graph.GetClass(), TEXT("CurrentDatabaseTags"));
		// CMC 가 재생 역할(Start/Pivot/Stop)을 태그에 넣으므로 원본 DB 태그 대신 그래프 값을 읽는다.
		OutTags = SelectedTags && CastField<FNameProperty>(SelectedTags->Inner)
			? *SelectedTags->ContainerPtrToValuePtr<TArray<FName>>(&Graph)
			: (OutDatabase ? OutDatabase->Tags : TArray<FName>());
	}
}

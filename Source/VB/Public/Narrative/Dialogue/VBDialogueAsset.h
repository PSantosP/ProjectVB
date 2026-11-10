// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBDialogueAsset.generated.h"

class UVBSpeakerDefinition;
struct FVBRuleContext;

/** 선택지 하나. 고르면 효과를 적용하고 NextNodeId 로 간다(비면 대화가 끝난다). */
USTRUCT(BlueprintType)
struct VB_API FVBDialogueChoice
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue", meta=(MultiLine=true))
	FText Text;

	// 전부 참일 때만 고를 수 있다. 판단은 서버가 한다 - 평판처럼 클라가 못 읽는 조건이 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 조건이 거짓일 때 잠긴 채 보여 주나(참) 숨기나(거짓). 「평판이 모자라서 못 하는 말」 을 보여 주는 것도 이야기다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	bool bShowWhenUnavailable = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Effects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	FName NextNodeId;
};

/** 대사 한 마디. 선택지가 있으면 고른 것을 따라가고, 없으면 NextNodeId 로 간다(비면 끝). */
USTRUCT(BlueprintType)
struct VB_API FVBDialogueNode
{
	GENERATED_BODY()

	// 이 대화 안에서만 유일하면 된다. 연결(NextNodeId)이 이 이름을 가리킨다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	FName NodeId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue", meta=(Categories="Speaker"))
	FGameplayTag Speaker;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue", meta=(MultiLine=true))
	FText Line;

	// 이 노드에 들어설 때마다 적용한다(서버). 같은 노드로 되돌아오는 대화라면 다시 적용된다 - 한 번만이어야 하면 사실 조건을 함께 건다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnEnterEffects;

	// 비어 있지 않으면 이 노드에 들어설 때 Event.Dialogue.NodeReached 를 방송한다(대상 태그 = 이것). 퀘스트의 「누구와 이 얘기를 했다」 가 듣는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	FGameplayTag ReachedTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	TArray<FVBDialogueChoice> Choices;

	// 선택지가 없을 때만 쓴다. 비면 이 노드가 마지막이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	FName NextNodeId;
};

/** 대화의 시작점. 위에서부터 조건이 처음 참인 것으로 들어간다 - 「처음 만남」 과 「다시 만남」 을 가르는 자리다. */
USTRUCT(BlueprintType)
struct VB_API FVBDialogueEntryPoint
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	FName StartNodeId;
};

/**
 * 대화 하나(실행 중 불변). 노드는 배열로 두고 조건, 효과는 규칙(VBRules)을 그대로 쓴다.
 * 저장하지 않는다 - 대화가 남기는 것은 효과가 바꾼 사실, 평판이 들고 간다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBDialogueAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 이 대화에 나오는 화자. 노드의 Speaker 태그를 이름, 초상화로 푸는 표다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	TArray<TObjectPtr<const UVBSpeakerDefinition>> Speakers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	TArray<FVBDialogueEntryPoint> EntryPoints;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	TArray<FVBDialogueNode> Nodes;

	// 적이 플레이어를 노리는 동안에도 열리나. 기본은 꺼짐 - 보스 도발처럼 싸우는 중 대화만 켠다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	bool bAllowDuringCombat = false;

	// 조건이 처음 참인 진입점의 시작 노드. 없으면 NAME_None - 이 사람은 지금 할 말이 없다.
	FName ChooseEntry(const FVBRuleContext& Context) const;

	const FVBDialogueNode* FindNode(FName NodeId) const;
	const UVBSpeakerDefinition* FindSpeaker(FGameplayTag SpeakerId) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

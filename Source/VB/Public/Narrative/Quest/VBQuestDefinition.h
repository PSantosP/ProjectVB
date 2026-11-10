// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBQuestDefinition.generated.h"

class UVBDialogueAsset;
struct FVBGameEvent;
struct FVBRuleContext;

/**
 * 퀘스트 목표의 바탕. InstancedStruct 다형이라 새 목표 종류 = 파생 구조체 하나.
 * 사건을 세는 목표(처치, 줍기, 말하기)와 지금 상태로 판정하는 목표(아이템 N개 갖기)가 있다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBQuestObjective
{
	GENERATED_BODY()

	virtual ~FVBQuestObjective() = default;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(MultiLine=true))
	FText Description;

	// 선택 목표 - 단계 완료에 필요 없다(일지에 보이기만).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest")
	bool bOptional = false;

	// 이 목표가 가리키는 월드 마커 태그(UVBWorldMarkerComponent::MarkerTag 와 같아야 함). 비면 마커 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(Categories="Marker"))
	FGameplayTag MarkerTag;

	// 단계 시작 순간 이미 찬 목표면 이 대화를 열고, 확인 노드(ReachedTag == AlreadyMetAckTag)에 닿을 때까지 다음 단계로 진행하지 않는다.
	// 둘 중 하나라도 비면 기다리지 않는다. 사건 목표에는 쓰이지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest")
	TObjectPtr<const UVBDialogueAsset> AlreadyMetDialogue;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest")
	FGameplayTag AlreadyMetAckTag;

	bool WaitsForAcknowledgement() const { return AlreadyMetDialogue && AlreadyMetAckTag.IsValid(); }

	// 채워야 하는 수. 상태 목표는 1(참이면 다 찬 것).
	virtual int32 GetRequiredCount() const { return 1; }

	// 사건 하나가 이 목표를 몇 칸 채우나. 0 = 이 목표와 상관없는 사건.
	virtual int32 CountEvent(const FVBGameEvent& Event) const { return 0; }

	// 지금 상태로 판정하는 목표인가. 참이면 진행 = Evaluate 의 결과(세지 않는다).
	virtual bool IsStateBased() const { return false; }
	virtual bool Evaluate(const FVBRuleContext& Context) const { return false; }
};

/** 사건을 센다. 사건 태그가 계층으로 맞고(Event.Enemy 는 Event.Enemy.Killed 를 품는다) 대상 태그가 질의를 만족하면 사건의 수만큼. */
USTRUCT(BlueprintType, meta=(DisplayName="사건 세기"))
struct VB_API FVBQuestObjective_Event : public FVBQuestObjective
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(Categories="Event"))
	FGameplayTag EventTag;

	// 사건의 대상 태그(죽은 적의 종류, 주운 아이템, 대사 노드의 태그)에 거는 질의. 비면 아무 대상이나.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest")
	FGameplayTagQuery TargetQuery;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(ClampMin="1"))
	int32 RequiredCount = 1;

	virtual int32 GetRequiredCount() const override { return FMath::Max(1, RequiredCount); }
	virtual int32 CountEvent(const FVBGameEvent& Event) const override;
	// 저작 문제 한 줄(빈 = 문제 없음). 퀘스트, 안내 검증이 같이 부른다.
	FString ValidateSetup() const;
};

/** 지금 상태가 조건을 만족하나(2단계 규칙). 사건이 올 때마다, 사실이 바뀔 때마다, 단계가 시작될 때 다시 본다. */
USTRUCT(BlueprintType, meta=(DisplayName="조건"))
struct VB_API FVBQuestObjective_Condition : public FVBQuestObjective
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	virtual bool IsStateBased() const override { return true; }
	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/** 단계 하나. 필수 목표가 다 차면 효과를 적용하고 다음 단계로 간다. 단계는 순서대로다 - 분기는 사실로 푼다(설계 3.3절). */
USTRUCT(BlueprintType)
struct VB_API FVBQuestStage
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(MultiLine=true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest",
		meta=(BaseStruct="/Script/VB.VBQuestObjective", ExcludeBaseStruct))
	TArray<FInstancedStruct> Objectives;

	// 이 단계를 마칠 때 적용한다(서버, 부른 플레이어 = 퀘스트 소유자).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnCompleteEffects;
};

/**
 * 퀘스트 하나의 정의(실행 중 불변). 자산 ID = (VBQuest, QuestId 태그 이름), 저장은 QuestId 로 한다.
 * 시작은 FVBEffect_StartQuest 효과로만, 진행은 사건으로만 한다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBQuestDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetQuestAssetType();
	static FPrimaryAssetId MakeQuestAssetId(FGameplayTag InQuestId);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(Categories="Quest"))
	FGameplayTag QuestId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(MultiLine=true))
	FText Summary;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest")
	TArray<FVBQuestStage> Stages;

	// 마지막 단계를 마치면 적용한다. 보상 전용 타입을 두지 않는다 - 아이템, 사실, 평판이 다 효과로 적힌다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Rewards;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

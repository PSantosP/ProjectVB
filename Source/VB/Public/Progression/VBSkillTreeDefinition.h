// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBSkillTreeDefinition.generated.h"

class UGameplayAbility;
class UTexture2D;

/** 스킬 트리의 칸 하나. 능력치 덧셈(Stats)과 능력 해금(GrantedAbility) 중 하나 또는 둘 다를 한다. */
USTRUCT(BlueprintType)
struct VB_API FVBSkillNode
{
	GENERATED_BODY()

	// 칸의 이름. 저장, 선행 조건, 사건 대상이 이 태그로 부른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(Categories="Skill.Node"))
	FGameplayTag NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth")
	TSoftObjectPtr<UTexture2D> Icon;

	// 여는 데 드는 스킬 포인트(1 이상).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(ClampMin="1"))
	int32 Cost = 1;

	// 이 레벨 이상이어야 연다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(ClampMin="1"))
	int32 RequiredLevel = 1;

	// 전부 열려 있어야 연다(같은 트리의 NodeId).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(Categories="Skill.Node"))
	TArray<FGameplayTag> Prerequisites;

	// 능력치 태그 -> 덧셈값. 연 칸의 값을 모두 더해 StatBonusEffect 하나로 건다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(Categories="Data.Stat"))
	TMap<FGameplayTag, float> Stats;

	// 열면 주는 능력. 시작 능력에도 있으면 성장 컴포넌트가 경고한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth")
	TSubclassOf<UGameplayAbility> GrantedAbility;

	// 스킬 화면의 자리(캔버스 좌표, 칸 중심).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth")
	FVector2D LayoutPosition = FVector2D::ZeroVector;
};

/**
 * 스킬 트리 하나(주인공 Kael). 칸 목록이 전부라 칸 추가, 수치 변경에 코드가 필요 없다.
 * 판정은 VBGrowth::CanUnlock, 적용은 UVBProgressionComponent.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBSkillTreeDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Growth", meta=(TitleProperty="NodeId"))
	TArray<FVBSkillNode> Nodes;

	const FVBSkillNode* FindNode(const FGameplayTag& NodeId) const;

	// 데이터 문제 목록(비면 정상). IsDataValid 와 단위 시험이 같이 쓴다.
	static TArray<FText> FindProblems(const UVBSkillTreeDefinition& Tree);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

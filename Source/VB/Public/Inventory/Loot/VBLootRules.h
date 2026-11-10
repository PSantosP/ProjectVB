// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBLootRules.generated.h"

class UVBItemDefinition;
class UVBLootTable;
struct FRandomStream;

/** 굴린 결과 한 줄. 스택에서만 사는 값이라 UObject 를 맨 포인터로 든다. */
struct FVBLootDrop
{
	const UVBItemDefinition* Item = nullptr;
	int32 Count = 0;
};

/** 보상표 굴리기. */
namespace VBLoot
{
	// 조건이 참이고 확률을 통과한 줄마다 [MinCount, MaxCount] 개를 뽑는다. 같은 아이템은 한 줄로 합친다.
	//   난수를 인자로 받는 것은 시험이 씨앗을 고정하기 위해서다.
	VB_API TArray<FVBLootDrop> Roll(const UVBLootTable& Table, const FVBRuleContext& Context, FRandomStream& Stream);

	// 남은 줄을 Give 로 넣고 넣은 만큼 뺀다(서랍장 남은 몫). 못 넣은 몫은 잃지 않고 남긴다. 넣은 총수를 돌려준다.
	VB_API int32 GiveStacks(TArray<FVBSavedItem>& InOutRemaining, TFunctionRef<int32(const FVBSavedItem&)> Give);
}

/**
 * 보상표를 굴려 부른 플레이어에게 넣는다. 넘친 몫은 상대(Counterpart) 발밑에 떨군다(상대가 없으면 경고 후 버림).
 * 서랍장에는 달지 않는다 - AVBLootContainer 가 남은 몫과 함께 표를 직접 든다. 한 적에는 하나만 단다(떨군 자리가 겹친다).
 */
USTRUCT(BlueprintType, meta=(DisplayName="보상표 주기"))
struct VB_API FVBEffect_GiveLoot : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Loot")
	TObjectPtr<const UVBLootTable> Table;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

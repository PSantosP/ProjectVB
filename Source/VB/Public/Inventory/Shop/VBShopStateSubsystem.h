// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SaveSystem/VBSavedWorldSubsystem.h"
#include "VBShopStateSubsystem.generated.h"

/**
 * 상점 재고를 관리한다: (상점 ID, 아이템 ID) -> 판 수. 판 수를 기록해야 재고를 늘린 패치가 이전 저장에도 적용된다.
 * 스트리밍에도 남도록 월드 서브시스템에 둔다. 서버 전용, 복제 안 함(클라는 거래 보기로 받는다).
 */
UCLASS()
class VB_API UVBShopStateSubsystem : public UVBSavedWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVBShopStateSubsystem* Get(const UObject* WorldContextObject);


	int32 GetSold(FGameplayTag ShopId, FGameplayTag ItemId) const;

	// 서버에서만. 거래가 끝난 뒤 판 수를 늘린다.
	void AddSold(FGameplayTag ShopId, FGameplayTag ItemId, int32 Count);

	//~ IVBSaveParticipant - 세계의 것이라 문맥의 플레이어와 무관하게 통째로 쓰고 읽는다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

private:
	struct FStockKey
	{
		FGameplayTag ShopId;
		FGameplayTag ItemId;

		bool operator==(const FStockKey& Other) const { return ShopId == Other.ShopId && ItemId == Other.ItemId; }
		friend uint32 GetTypeHash(const FStockKey& Key) { return HashCombine(GetTypeHash(Key.ShopId), GetTypeHash(Key.ItemId)); }
	};

	bool HasWriteAuthority() const;

	// 태그는 UObject 가 아니라 GC 가 볼 것이 없다 - UPROPERTY 가 아니어도 된다.
	TMap<FStockKey, int32> Sold;
};

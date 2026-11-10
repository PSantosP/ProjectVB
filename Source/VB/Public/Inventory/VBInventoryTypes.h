// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "VBInventoryTypes.generated.h"

class UVBItemDefinition;
class UVBInventoryComponent;

/** 가진 아이템 한 줄(한 정의에 한 줄). */
USTRUCT()
struct FVBInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBItemDefinition> Definition = nullptr;

	UPROPERTY()
	int32 Count = 0;

	void PostReplicatedAdd(const struct FVBInventoryList& List);
	void PostReplicatedChange(const struct FVBInventoryList& List);
	void PreReplicatedRemove(const struct FVBInventoryList& List);
};

/** 복제되는 목록. 바뀐 줄만 보낸다(FFastArraySerializer). */
USTRUCT()
struct FVBInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVBInventoryEntry> Entries;

	// 복제 콜백이 알릴 소유 컴포넌트. 복제하지 않는다.
	UPROPERTY(NotReplicated)
	TObjectPtr<UVBInventoryComponent> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVBInventoryEntry, FVBInventoryList>(Entries, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVBInventoryList> : public TStructOpsTypeTraitsBase2<FVBInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerState;
class UVBInventoryComponent;
class UVBItemDefinition;

// 아이템을 월드와 캐릭터에 연결하는 서버 동작: 사용 시작과 버린 몫 떨구기. 수량 쓰기는 UVBInventoryComponent 의 공개 함수로만 한다.
namespace VBItemActions
{
	// 서버 쪽 사용의 시작 하나(퀵슬롯, 화면). 몽타주가 없으면 바로 ConfirmUse, 있으면 폰 ASC 에 Event.Item.Use 를 보내 사용 GA 를 켠다.
	void BeginUse(UVBInventoryComponent& Inventory, APlayerState* PlayerState, const UVBItemDefinition* Item);

	// 땅의 아이템을 먼저 만들고 성공했을 때만 뺀다(조용히 사라지지 않게). 버렸으면 true.
	bool Discard(UVBInventoryComponent& Inventory, const APlayerState* PlayerState, const UVBItemDefinition* Item, int32 Count);
}

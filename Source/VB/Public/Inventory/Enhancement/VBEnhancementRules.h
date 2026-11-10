// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBEnhancementRules.generated.h"

class UVBItemDefinition;
struct FVBItemFragment_Equippable;

// 무엇을 강화하나. 무기(종류) 또는 아이템(정의) 중 정확히 하나. 아이템은 인스턴스가 없어 정의 하나에 단계 하나다 - 같은 아이템 여러 개는 단계를 나눠 갖는다.
USTRUCT(BlueprintType)
struct VB_API FVBEnhanceTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Enhancement")
	EVBWeaponType Weapon = EVBWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Enhancement")
	TObjectPtr<const UVBItemDefinition> Item;

	static FVBEnhanceTarget ForWeapon(EVBWeaponType InWeapon);
	static FVBEnhanceTarget ForItem(const UVBItemDefinition* InItem);
	bool IsWeapon() const { return Weapon != EVBWeaponType::None && !Item; }
	bool IsItem() const { return Item && Weapon == EVBWeaponType::None; }
	bool IsValid() const { return IsWeapon() || IsItem(); }
	bool operator==(const FVBEnhanceTarget& Other) const { return Weapon == Other.Weapon && Item == Other.Item; }
};

UENUM(BlueprintType)
enum class EVBEnhanceResult : uint8
{
	Ok,
	Invalid, // 대상이 정확히 하나가 아니다
	NotEnhanceable, // 갈래가 없다(장비 아님, 능력치가 모두 0)
	NotOwned,
	MaxLevel,
	NoTrack, // 수치 자산이 없거나 그 갈래의 트랙이 틀렸다 - 비용을 받지 않는다
	MissingMaterials,
	NotEnoughGold,
};

// 강화 판정에 필요한 사실. 서버가 채운다(c2 의 제작 컴포넌트). 판정은 순수 함수 하나(VBEnhancement::Check).
struct FVBEnhanceQuote
{
	bool bTargetValid = false;
	EVBEnhanceCategory Category = EVBEnhanceCategory::None;
	bool bOwned = false;
	int32 CurrentLevel = 0;
	int32 MaxLevel = 0;
	bool bTrackUsable = false;
	bool bHasMaterials = false;
	int32 GoldRequired = 0;
	int32 GoldHeld = 0;
};

namespace VBEnhancement
{
	// 무기 = Weapon. 아이템 = 장비 조각의 칸이 머리, 몸통, 팔이면 Armor, 장신구면 Accessory. 0 이 아닌 능력치가 없으면 None(키울 것이 없다).
	VB_API EVBEnhanceCategory CategoryOf(const FVBEnhanceTarget& Target);
	VB_API bool HasScalableStats(const FVBItemFragment_Equippable& Equippable);

	// [0, MaxLevel] 로 자른다. MaxLevel 이 0 이하(수치 자산 없음)면 0 아래만 자른다.
	VB_API int32 NormalizeLevel(int32 Level, int32 MaxLevel);

	// Level 의 배율. 0 이면 1. 트랙이 없거나 그 단계가 없으면 1 - 적용 쪽은 막을 수 없으니 1 로 두고, 강화(비용) 쪽은 Check 가 막는다.
	VB_API float ScaleAt(const FVBEnhanceTrack* Track, int32 Level);

	VB_API EVBEnhanceResult Check(const FVBEnhanceQuote& Quote);

	// 값마다 Scale 을 곱한다. 부호는 그대로다(받는 피해 감소처럼 음수 보너스는 더 큰 감소가 된다).
	VB_API TMap<FGameplayTag, float> ScaledStats(const TMap<FGameplayTag, float>& Stats, float Scale);

	VB_API const TCHAR* LexToString(EVBEnhanceResult Result);
}

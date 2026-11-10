// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "VBAppearanceSettings.generated.h"

/**
 * 장비 칸 -> 외형 칸 대응 표(Project Settings > Game > VB Appearance).
 * 외형 조각은 자기 장비 칸에 대응하는 칸만 채우므로, 두 조각이 한 칸을 다투는 것은 깨진 데이터뿐이다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Appearance"))
class VB_API UVBAppearanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Equipment.Slot.* -> Appearance.Slot.*. 없는 장비 칸(장신구)은 겉모습을 바꾸지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Appearance", meta=(Categories="Equipment.Slot"))
	TMap<FGameplayTag, FGameplayTag> EquipmentToAppearanceSlot;

	// 이 장비 칸이 채우는 외형 칸. 없으면 빈 태그.
	FGameplayTag FindAppearanceSlot(const FGameplayTag& EquipmentSlot) const;
};

namespace VBAppearanceSettingsRules
{
	// 표 검증: 값은 Appearance.Slot 아래의 서로 다른 칸이다. 배치에 두 번 나오는 장비 칸(장신구)은 외형 칸이 없어야 한다.
	VB_API TArray<FText> FindTableProblems(const TMap<FGameplayTag, FGameplayTag>& Table, const TArray<FGameplayTag>& SlotLayout);

	// V7: 외형 조각을 든 아이템은 장비이고, 조각의 칸이 그 장비 칸이 대응하는 외형 칸이다. 문제가 없으면 빈 글.
	//   bHasEquippable=false 면 EquipmentSlot, MappedSlot 은 보지 않는다.
	VB_API FText FindItemPieceProblem(bool bHasEquippable, const FGameplayTag& EquipmentSlot, const FGameplayTag& MappedSlot,
		const FGameplayTag& PieceSlot);

	// 캐릭터에 보이는 칸의 장비는 외형 조각을 가진다 - 조각이나 '겉모습 없음' 중 하나만. 문제가 없으면 빈 글.
	VB_API FText FindItemLookDecisionProblem(bool bHasEquippable, const FGameplayTag& EquipmentSlot, const FGameplayTag& MappedSlot,
		bool bHasAppearanceFragment, bool bNoAppearance, bool bNamesPiece);
}

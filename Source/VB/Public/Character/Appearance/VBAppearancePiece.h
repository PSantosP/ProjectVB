// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBAppearancePiece.generated.h"

class USkeletalMesh;
class UVBItemDefinition;

/**
 * 캐릭터 메시 위에 끼우는 옷, 장비 조각 하나(캐릭터마다 맞춘 메시를 가진다). 칸은 이 자산의 AppearanceSlot 하나가 정한다.
 * 멤버가 EditAnywhere 인 이유: 파이썬 저작 경로가 EditDefaultsOnly 를 거부한다.
 */
UCLASS(BlueprintType)
class VB_API UVBAppearancePiece : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 이 조각이 차지하는 외형 칸(Appearance.Slot.*).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Appearance", meta=(Categories="Appearance.Slot"))
	FGameplayTag AppearanceSlot;

	// 캐릭터 이름(UVBCharacterAppearance::CharacterId) -> 그 캐릭터에 맞춘 메시. 캐릭터마다 크기, 성별이 달라 한 메시를 같이 쓰지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Appearance")
	TMap<FName, TSoftObjectPtr<USkeletalMesh>> FittedMeshes;

	// 이 조각이 덮어 숨기는 신체 부위(Appearance.Section.*). 마지막 이름 = 바디 메시의 머티리얼 슬롯 이름.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Appearance", meta=(Categories="Appearance.Section"))
	FGameplayTagContainer HidesSections;

	// 이 캐릭터에 맞춘 메시. 없으면 null 경로(이 캐릭터는 이 조각을 입을 수 없다).
	TSoftObjectPtr<USkeletalMesh> FindFittedMesh(FName CharacterId) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** 외형 칸의 판정부 - 자산 없이 시험한다. 문제 하나에 한 줄. */
namespace VBAppearancePieceRules
{
	// 칸 순서의 아이템 상태(작은 DA 는 권위 쪽 호출자가 이미 풀었다). 규칙 안에서는 로드하지 않는다.
	struct FEquippedPiece
	{
		const UVBItemDefinition* Item = nullptr;
		const UVBAppearancePiece* Piece = nullptr;
	};
	struct FPieceProjection
	{
		TArray<const UVBAppearancePiece*> Pieces;
		TArray<FText> Errors;
	};
	struct FPieceChoice
	{
		TArray<const UVBAppearancePiece*> Pieces;
		TArray<FText> Warnings;
		TArray<FText> Errors;
	};
	VB_API FPieceProjection ProjectEquippedPieces(const TArray<FEquippedPiece>& Equipped);
	VB_API FPieceChoice ChoosePieces(FName CharacterId, const TArray<const UVBAppearancePiece*>& Defaults,
		const TArray<const UVBAppearancePiece*>& Equipped);

	// 장착한 조각을 지금 겉모습이 입을 수 없는 이유. 입을 수 있거나 겉모습을 아직 읽는 중이면 빈 텍스트.
	VB_API FText FindUnwearableReason(bool bHasLoadedLook, bool bLookRequested, FName CharacterId, int32 EquippedCount);

	// 조각 하나의 판정에 필요한 사실만.
	struct FPieceFacts
	{
		FString Name;
		FGameplayTag Slot;
		FGameplayTagContainer Hides;
	};

	// 신체 부위 일곱의 이름(Appearance.Section.* 의 마지막 이름) - 바디 메시의 머티리얼 슬롯 이름이 이것이어야 한다.
	VB_API const TArray<FName>& BodySectionNames();

	// 섹션이 실제로 쓰는 머티리얼 번호. LOD 머티리얼 맵의 재매핑이 이긴다(엔진과 같은 순서). INDEX_NONE = 재매핑 없음.
	VB_API int32 EffectiveMaterialIndex(int32 SectionMaterialIndex, const TArray<int32>& LODMaterialMap, int32 SectionIndex);

	// LOD 마다, 섹션마다 실제 머티리얼 슬롯 이름. 렌더 데이터가 없으면 빈 배열.
	VB_API TArray<TArray<FName>> SectionSlotNamesPerLod(const USkeletalMesh& Mesh);

	// V3: LOD0 에 일곱 이름이 다 있고, 모든 LOD 에서 섹션들이 일곱 이름 중 서로 다른 이름을 쓴다(두 구역이 한 슬롯으로 합쳐지면 따로 숨길 수 없다).
	VB_API TArray<FText> FindBodySectionProblems(const TArray<TArray<FName>>& NamesPerLod);

	// 숨길 섹션 번호를 LOD 마다. 이름으로 고른다 - 섹션 번호가 LOD 마다 같은 구역이라는 가정을 하지 않는다.
	VB_API TArray<TArray<int32>> SectionsToHide(const TArray<TArray<FName>>& NamesPerLod, const TSet<FName>& HiddenNames);

	// V4, V5: 기본 조각들의 칸이 서로 다르고 상의, 하의가 있다. 맨몸 메시는 목이 없어 상의 없이는 틈이 난다.
	VB_API TArray<FText> FindDefaultPieceProblems(const TArray<FPieceFacts>& Pieces);
}

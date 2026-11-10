// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBPassiveSetDefinition.generated.h"

class UTexture2D;

/** 귀속 아이템이 주는 패시브 하나 - 레벨업 스킬 트리와 별개다. 능력을 여는 일은 전투 규칙 표가 이 태그로 한다. */
USTRUCT(BlueprintType)
struct VB_API FVBPassiveDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(Categories="Passive"))
	FGameplayTag PassiveTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive")
	TSoftObjectPtr<UTexture2D> Icon;

	// 얻었을 때 보여 줄 입력. 키 모양은 지금 매핑에서 읽는다. 비면 안내 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(Categories="Input"))
	FGameplayTag HintInputTag;

	// 패시브 화면의 자리(캔버스 좌표, 칸 중심).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive")
	FVector2D LayoutPosition = FVector2D::ZeroVector;
};

/** 얻은 순간의 연출 시간(초, 줍기부터). 위젯이 읽는다. */
USTRUCT(BlueprintType)
struct VB_API FVBPassiveRevealTiming
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(ClampMin="0", Units="s"))
	float DipStart = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(ClampMin="0", Units="s"))
	float PanelStart = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(ClampMin="0", Units="s"))
	float TextStart = 0.95f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(ClampMin="0", Units="s"))
	float HintStart = 1.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(ClampMin="0.1", Units="s"))
	float End = 2.0f;
};

/** 주인공이 얻을 수 있는 패시브 전부. UVBPassiveComponent 가 이 목록에 있는 태그만 준다. */
UCLASS(BlueprintType, Const)
class VB_API UVBPassiveSetDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Passive", meta=(TitleProperty="PassiveTag"))
	TArray<FVBPassiveDefinition> Passives;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Passive")
	FVBPassiveRevealTiming RevealTiming;

	const FVBPassiveDefinition* FindPassive(const FGameplayTag& PassiveTag) const;

	// 저작 문제 - 하나에 한 줄.
	TArray<FText> FindProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

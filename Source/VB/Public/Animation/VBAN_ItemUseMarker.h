// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "VBAN_ItemUseMarker.generated.h"

class UAnimMontage;

/**
 * 아이템 사용 확정 시각. 서버에서 Event.Item.UseMarker 를 보내 UVBGA_UseItem 이 소모, 효과를 확정한다.
 * 이 시각 전에 끊기면 아무것도 소모하지 않는다. 몽타주마다 정확히 하나여야 한다.
 */
UCLASS(DisplayName = "Item Use Marker")
class VB_API UVBAN_ItemUseMarker : public UAnimNotify
{
	GENERATED_BODY()

public:
	UVBAN_ItemUseMarker();

	// 몽타주와 그 슬롯 조각 애니에 놓인 표식 수. 조각 애니의 노티도 재생 중 발화하므로 함께 센다.
	static int32 CountIn(const UAnimMontage& Montage);

	// 사용 몽타주의 규칙 위반 목록(비면 유효): 몽타주 자체에 표식 하나, 블렌드아웃보다 앞, 반드시 끝남.
	static TArray<FText> FindUseMontageProblems(const UAnimMontage& Montage);

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};

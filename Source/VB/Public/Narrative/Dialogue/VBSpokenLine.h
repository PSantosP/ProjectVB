// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBSpokenLine.generated.h"

class USoundBase;
class UVBSpeakerDefinition;

/**
 * 조종을 멈추지 않는 한 줄(혼잣말, 짧은 외침). 대화와 달리 화면을 열지 않고 자막 한 줄만 띄운다.
 * 줄은 쌓지 않고 새 줄이 오면 갈아 끼운다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBSpokenLine
{
	GENERATED_BODY()

	// 비면 화자 이름 칸을 접는다(주인공 혼잣말).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	TObjectPtr<const UVBSpeakerDefinition> Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue", meta=(MultiLine=true))
	FText Text;

	// 음성(선택). 소유 클라에서 2D 로 한 번 재생한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	TObjectPtr<USoundBase> Voice;

	// 보이는 시간(초). 0 이면 글자 수로 정한다(UVBSpokenLineComponent 의 읽기 속도).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue", meta=(ClampMin="0.0"))
	float Duration = 0.0f;
};

/**
 * 부른 플레이어의 화면에 한 줄을 띄운다. 조건이 거짓이면 아무것도 안 한다.
 * 한 번만 말하려면 조건에 사실 X 꺼짐, 효과 목록에 사실 X 켜기를 함께 둔다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="줄 말하기"))
struct VB_API FVBEffect_SpeakLine : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	FVBSpokenLine Line;

	// 말하기 직전에 본다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

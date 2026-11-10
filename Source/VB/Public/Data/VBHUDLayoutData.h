// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Widgets/Layout/Anchors.h"
#include "VBHUDLayoutData.generated.h"

class UUserWidget;

// HUD 루트 캔버스에 놓는 슬롯 자리 하나. 행 순서 = 붙이는 순서 = 같은 ZOrder 안 그리기 순서.
USTRUCT(BlueprintType)
struct VB_API FVBUISlotPlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|UI", meta=(Categories="UI.Slot"))
	FGameplayTag Slot;

	// (0,0)-(1,1) 이면 전체 화면(아래 Position, Size 무시, 여백 0). 아니면 점 앵커.
	UPROPERTY(EditAnywhere, Category="VB|UI")
	FAnchors Anchors;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	FVector2D Alignment = FVector2D::ZeroVector;

	// 점 앵커에서의 거리(위젯 단위).
	UPROPERTY(EditAnywhere, Category="VB|UI")
	FVector2D Position = FVector2D::ZeroVector;

	// 점 앵커이고 AutoSize 가 꺼졌을 때의 크기.
	UPROPERTY(EditAnywhere, Category="VB|UI")
	FVector2D Size = FVector2D(100.0f, 30.0f);

	// 켜면 자리 크기 = 항목이 원하는 크기.
	UPROPERTY(EditAnywhere, Category="VB|UI")
	bool bAutoSize = true;

	// HUD 루트 캔버스의 그리기 구간. 0 = 줄들, -1 = 맨 아래(월드 표시기 층), 100 = 맨 위(무기 바퀴).
	UPROPERTY(EditAnywhere, Category="VB|UI")
	int32 ZOrder = 0;

	bool IsFullScreen() const;
};

// 슬롯에 기본으로 띄우는 위젯 하나. HUD 가 범위 = 자기 자신으로 등록한다.
USTRUCT(BlueprintType)
struct VB_API FVBUISlotDefaultWidget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|UI", meta=(Categories="UI.Slot"))
	FGameplayTag Slot;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	TSubclassOf<UUserWidget> WidgetClass;

	// 같은 슬롯 안 그리기 순서(큰 값이 위).
	UPROPERTY(EditAnywhere, Category="VB|UI")
	int32 Order = 0;

	// 켜면 클래스가 비어도 말하지 않는다 - 끄는 길이 비우기인 줄(레벨 줄)용.
	UPROPERTY(EditAnywhere, Category="VB|UI")
	bool bOptional = false;

	// 새로 만든 항목에 붙인 직후 한 번 적용(입력을 안 받는 전체 화면 줄 = HitTestInvisible).
	UPROPERTY(EditAnywhere, Category="VB|UI")
	bool bSetInitialVisibility = false;

	UPROPERTY(EditAnywhere, Category="VB|UI", meta=(EditCondition="bSetInitialVisibility"))
	ESlateVisibility InitialVisibility = ESlateVisibility::HitTestInvisible;
};

/**
 * HUD 배치(DA_HUDLayout). 무엇을 어디에 띄울지의 홈 - HUD 코드는 슬롯 자리를 만들고 기본 위젯을 등록할 뿐이다.
 * 다른 기능은 같은 슬롯에 런타임 등록으로 더한다(UVBUISlotSubsystem).
 */
UCLASS(BlueprintType)
class VB_API UVBHUDLayoutData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="VB|UI")
	TArray<FVBUISlotPlacement> Slots;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	TArray<FVBUISlotDefaultWidget> Widgets;

	// 저작 검사와 런타임 경고가 같이 쓰는 한 곳: 빈, 중복 슬롯 태그, 자리 없는 기본 위젯, 필수인데 빈 클래스.
	void CollectProblems(TArray<FString>& OutProblems) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

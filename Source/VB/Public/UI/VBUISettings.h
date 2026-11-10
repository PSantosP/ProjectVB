// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Fonts/SlateFontInfo.h"
#include "GameplayTagContainer.h"
#include "VBUISettings.generated.h"

class UVBPrimaryLayout;
class UVBScreenWidget;

/** 화면 태그 하나가 어느 층에 어떤 위젯으로 뜨는가. */
USTRUCT(BlueprintType)
struct FVBScreenRoute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|UI", meta=(Categories="UI.Layer"))
	FGameplayTag Layer;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	TSoftClassPtr<UVBScreenWidget> ScreenClass;
};

/**
 * UI 층의 프로젝트 전역 설정(Project Settings > Game > VB UI).
 * 여는 쪽은 화면 태그만 알고, 태그 -> 층, 위젯 표는 여기 한 곳이다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB UI"))
class VB_API UVBUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 로컬 플레이어마다 뷰포트에 하나 올라가는 뿌리 위젯(WBP_PrimaryLayout). 층 넷을 담는다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI")
	TSoftClassPtr<UVBPrimaryLayout> PrimaryLayoutClass;

	// 화면 태그 -> 층, 위젯. UVBUIManagerSubsystem::PushScreen 이 읽는 유일한 표다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI", meta=(Categories="UI.Screen", ForceInlineRow))
	TMap<FGameplayTag, FVBScreenRoute> Screens;

	// UI 조작(확인, 뒤로) 키를 담은 IMC. 캐릭터 없는 메뉴에서도 살아야 해서 UI 관리자가 붙인다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Input")
	TSoftObjectPtr<class UInputMappingContext> UIInputMapping;

	// 게임플레이 IMC(0)보다 높게 둔다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Input")
	int32 UIInputMappingPriority = 10;

	// 알림 줄 위젯(WBP_NoticeFeed). 층 위에 겹으로 붙는다. 비면 경고한다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Notice")
	TSoftClassPtr<class UUserWidget> NoticeFeedClass;

	// 알림 한 줄이 떠 있는 시간(실시간 초). 같은 줄에 합쳐지면 다시 잰다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Notice", meta=(ClampMin="0.5", Units="s"))
	float NoticeSeconds = 3.0f;

	// 한꺼번에 떠 있는 줄의 최대 수. 넘으면 가장 오래된 줄이 빠진다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Notice", meta=(ClampMin="1"))
	int32 MaxNotices = 4;

	// 로딩 화면 문구. 아래 값들은 만들 때 복사한다(로딩 스레드가 그리는 중 설정을 읽지 않는다).
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	FText LoadingText = NSLOCTEXT("VBUISettings", "LoadingText", "불러오는 중");

	// 글꼴 객체가 비면 엔진 기본 글꼴을 이 크기로 쓴다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	FSlateFontInfo LoadingTextFont;

	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	FLinearColor LoadingTextColor = FLinearColor::White;

	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	FLinearColor LoadingBackgroundColor = FLinearColor::Black;

	// 들어올 때 흐려짐(실시간 초). 0 = 바로 불투명.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading", meta=(ClampMin="0.0", Units="s"))
	float LoadingFadeInSeconds = 0.3f;

	// 아래 가운데 글, 도는 표시 묶음의 여백과 간격.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	FMargin LoadingContentPadding = FMargin(0.0f, 0.0f, 0.0f, 96.0f);

	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading", meta=(ClampMin="0.0"))
	float LoadingThrobberSpacing = 16.0f;

	// 뷰포트 위 순서. 알림 줄, 화면 층보다 위(Lyra 와 같은 값).
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	int32 LoadingZOrder = 10000;

	// 사유가 다 끝난 뒤 텍스처가 들어올 시간(실시간 초). 실제로 띄웠던 때만 기다린다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading", meta=(ClampMin="0.0", Units="s"))
	float LoadingHoldSeconds = 2.0f;

	// 이 시간을 초과하면 화면을 내리고 Error(못 띄웠으면 Warning). 같은 에피소드에서는 다시 띄우지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading", meta=(ClampMin="1.0", Units="s"))
	float LoadingMaxShowSeconds = 60.0f;

	// PIE 에서도 띄울지. PIE 는 맵 신호가 달라 기본은 끈다(켜도 입력은 막지 않는다).
	UPROPERTY(config, EditAnywhere, Category="VB|UI|Loading")
	bool bShowLoadingScreenInEditor = false;
};

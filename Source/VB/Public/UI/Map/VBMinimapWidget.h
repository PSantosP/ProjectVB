// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration
#include "Styling/SlateBrush.h"
#include "VBMinimapWidget.generated.h"

class AActor;
class APawn;
class APlayerState;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UVBMapDefinition;
class UVBOptionsSubsystem;
class UVBQuestLogComponent;
struct FVBSettingsSaveData;

enum class EVBMinimapIconKind : uint8
{
	SavePoint,
	Threat,
	Objective,
	ObjectiveRim,
	Player
};

// 한 프레임에 그릴 아이콘 하나. LocalOffset 은 가운데 기준 UI 단위, 각도는 위 = 0 시계 방향(도).
struct FVBMinimapDrawRecord
{
	EVBMinimapIconKind Kind = EVBMinimapIconKind::SavePoint;
	FVector2D LocalOffset = FVector2D::ZeroVector;
	float AngleDegrees = 0.0f;
	// 붓 색에 곱한다. 사라지는 중인 적 점만 1 보다 작다.
	float Opacity = 1.0f;
};

/**
 * HUD 의 원형 미니맵. 고정 Image 에 UI 머티리얼이 UV 를 돌려 그리고, 위 = 카메라 앞이다.
 * 아이콘은 NativeTick 이 프레임 기록을 만들고 NativePaint 가 그린다. 적 점은 이 폰과 싸우는 적만(서버 전용 정보라 원격 클라엔 없다).
 */
UCLASS()
class VB_API UVBMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 지도 정의를 정한다. 구성 때 한 번 부른다(동기 로드라 Tick 에서 안 부른다).
	void SetMapDefinition(const UVBMapDefinition* Definition);

	// 이번 프레임의 머티리얼 값과 그릴 것을 만든다. DeltaSeconds 는 적 점 페이드에만 쓴다.
	void UpdateFrame(const FGeometry& MyGeometry, float DeltaSeconds);

	// 이번 프레임에 그릴 것. 플레이어는 늘 마지막이다(맨 위에 그려진다).
	const TArray<FVBMinimapDrawRecord>& GetDrawRecords() const { return DrawRecords; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// UI 도메인 머티리얼. 파라미터 이름은 VBMap::BuildMinimapMaterialParams 와 같다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap")
	TObjectPtr<UMaterialInterface> MapMaterial;

	// 미니맵 가장자리까지의 월드 거리(cm). 줌은 이 값 하나다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap", meta=(ClampMin="100.0", Units="cm"))
	float WorldRadiusCm = 6000.0f;

	// 테두리 두께(UI 단위). 아이콘은 이만큼 안쪽에 머문다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap", meta=(ClampMin="0.0"))
	float BorderInset = 6.0f;

	// 붓은 위를 가리키게 그린다(화살표). 크기는 붓의 ImageSize.
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap")
	FSlateBrush PlayerBrush;

	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap")
	FSlateBrush ObjectiveBrush;

	// 목표가 테두리 밖이면 테두리 위에 이 화살표를 목표 쪽으로 돌려 그린다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap")
	FSlateBrush ObjectiveRimBrush;

	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap")
	FSlateBrush SavePointBrush;

	// 나와 싸우는 적(빨간 점).
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap")
	FSlateBrush ThreatBrush;

	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap", meta=(ClampMin="0.0", Units="s"))
	float ThreatFadeInSeconds = 0.15f;

	// 적 점이 사라지는 시간. 잠깐 풀린 적이 깜빡이지 않게 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Minimap", meta=(ClampMin="0.0", Units="s"))
	float ThreatFadeOutSeconds = 0.5f;

private:
	// 나와 싸우는(또는 방금까지 싸운) 적 하나. 파괴돼도 사라질 때까지 마지막 자리에 남는다.
	struct FThreat
	{
		TWeakObjectPtr<const AActor> Enemy;
		FVector2D LastXY = FVector2D::ZeroVector;
		float Opacity = 0.0f;
		bool bEngaged = false;
	};

	// 등록부에서 이번 프레임의 전투 여부를 다시 읽고 투명도를 한 걸음 옮긴다. 다 사라진 항목은 지운다.
	void UpdateThreats(const APawn& Pawn, float DeltaSeconds);
	void HandlePlayerStateSet(APlayerState* PlayerState);
	void HandleUserSettingsChanged(const FVBSettingsSaveData& Settings);
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	bool ShouldShow() const;
	void RefreshVisibility();
	void AddIcon(EVBMinimapIconKind Kind, const FVector2D& RawOffset, double LocalRadius, float Opacity = 1.0f);
	const FSlateBrush& BrushFor(EVBMinimapIconKind Kind) const;

	UPROPERTY()
	TObjectPtr<UImage> MapImage;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MapMaterialInstance;

	UPROPERTY()
	TObjectPtr<const UVBMapDefinition> MapDefinition;

	FVBPlayerStateSetRegistration PlayerStateSet;
	TWeakObjectPtr<UVBQuestLogComponent> QuestLog;
	TWeakObjectPtr<APlayerController> PawnSource;
	TWeakObjectPtr<UVBOptionsSubsystem> Options;
	FDelegateHandle UserSettingsChangedHandle;

	// 보이는 프레임마다 비우고 다시 채운다(용량은 재사용).
	TArray<FVBMinimapDrawRecord> DrawRecords;
	// 적 점. 보이는 프레임마다 등록부로 맞추고, 접히면 비운다.
	TArray<FThreat> Threats;
	// 이번 프레임 지도 정사각형의 반지름(UI 단위). NativePaint 가 같은 정사각형에 그린다.
	double MapLocalRadius = 0.0;
};

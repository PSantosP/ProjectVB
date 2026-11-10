// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Map/VBMinimapWidget.h"

#include "AI/VBEnemyCombatComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBEnemyRegistrySubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/VBSavePoint.h"
#include "Map/VBMapDefinition.h"
#include "Map/VBMapRules.h"
#include "Map/VBMapSettings.h"
#include "Map/VBTravelPointSubsystem.h"
#include "Map/VBWorldMarkerComponent.h"
#include "Map/VBWorldMarkerSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestMarkers.h"
#include "Rendering/DrawElements.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Settings/VBOptionsSubsystem.h"
#include "UI/VBScreenEdge.h"
#include "VB/VB.h"

namespace
{
	// 머티리얼과 맞춘 이름(M_UI_Minimap). 바꾸면 머티리얼도 같이 바꾼다.
	const FName MapTextureParam(TEXT("MapTexture"));
	const FName CenterUVParam(TEXT("CenterUV"));
	const FName RadiusOverExtentsParam(TEXT("RadiusOverExtents"));
	const FName YawCosSinParam(TEXT("YawCosSin"));

	FLinearColor ToParam(const FVector2D& Value)
	{
		return FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), 0.0f, 0.0f);
	}
}

void UVBMinimapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// 어느 크기의 칸에 놓여도 지도는 가운데 정사각형이다 - 아이콘도 같은 정사각형에 그린다(NativePaint).
		UScaleBox* Square = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("MapSquare"));
		Square->SetStretch(EStretch::ScaleToFit);
		MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapImage"));
		// 값이 아니라 비율(1:1)만 뜻이 있다 - ScaleToFit 가 칸 크기에 맞춘다. 크기는 DA_HUDLayout 칸이 정한다.
		MapImage->SetDesiredSizeOverride(FVector2D(1.0, 1.0));
		Square->AddChild(MapImage);
		WidgetTree->RootWidget = Square;
	}

	VB_CLOG(!MapImage, Warning, "미니맵: %s 가 자기 위젯 트리를 가져와 지도 칸이 없다 - 지도가 안 그려진다", *GetNameSafe(GetClass()));
	if (MapImage && MapMaterial)
	{
		MapMaterialInstance = UMaterialInstanceDynamic::Create(MapMaterial, this);
		MapImage->SetBrushFromMaterial(MapMaterialInstance);
	}

	VB_CLOG(!MapMaterial, Warning, "미니맵: %s 에 MapMaterial 이 없다 - 아이콘만 그려진다", *GetNameSafe(GetClass()));
	// 움직이는 동안 매 프레임 그림이 바뀐다 - 이 위젯만 휘발성으로 둔다(HUD 전체가 아니라).
	ForceVolatile(true);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성될 때마다 다시 잇는다(해체 때 푼다) - 같은 위젯이 다시 구성돼도 등록은 하나다.
	if (AVBPlayerController* PC = GetOwningPlayer<AVBPlayerController>())
	{
		PlayerStateSet.Register(*PC, FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBMinimapWidget::HandlePlayerStateSet));
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PawnSource = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UVBMinimapWidget::HandlePossessedPawnChanged);
	}

	if (UVBOptionsSubsystem* Found = UVBOptionsSubsystem::Get(GetOwningLocalPlayer()))
	{
		// 해체 없이 두 번 구성돼도 등록은 하나다.
		Found->OnUserSettingsChanged.Remove(UserSettingsChangedHandle);
		Options = Found;
		UserSettingsChangedHandle = Found->OnUserSettingsChanged.AddUObject(this, &UVBMinimapWidget::HandleUserSettingsChanged);
	}

	SetMapDefinition(UVBMapSettings::FindForWorld(GetWorld()));
}

void UVBMinimapWidget::NativeDestruct()
{
	PlayerStateSet.Unregister();
	QuestLog.Reset();
	if (APlayerController* PC = PawnSource.Get())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBMinimapWidget::HandlePossessedPawnChanged);
	}

	PawnSource.Reset();
	if (UVBOptionsSubsystem* Found = Options.Get())
	{
		Found->OnUserSettingsChanged.Remove(UserSettingsChangedHandle);
	}

	Options.Reset();
	UserSettingsChangedHandle.Reset();
	DrawRecords.Reset();
	Threats.Reset();
	Super::NativeDestruct();
}

void UVBMinimapWidget::SetMapDefinition(const UVBMapDefinition* Definition)
{
	MapDefinition = (Definition && Definition->IsUsable()) ? Definition : nullptr;
	if (MapDefinition && MapMaterialInstance)
	{
		MapMaterialInstance->SetTextureParameterValue(MapTextureParam, MapDefinition->Texture.Get());
	}

	RefreshVisibility();
}

void UVBMinimapWidget::HandlePlayerStateSet(APlayerState* /*PlayerState*/)
{
	QuestLog = UVBQuestLogComponent::FindFor(GetOwningPlayer());
}

void UVBMinimapWidget::HandleUserSettingsChanged(const FVBSettingsSaveData& /*Settings*/)
{
	RefreshVisibility();
}

void UVBMinimapWidget::HandlePossessedPawnChanged(APawn* /*OldPawn*/, APawn* /*NewPawn*/)
{
	RefreshVisibility();
}

bool UVBMinimapWidget::ShouldShow() const
{
	const APlayerController* PC = GetOwningPlayer();
	return MapDefinition
		&& PC && PC->GetPawn()
		&& UVBSaveGameSubsystem::GetSettingsData(this).bShowMinimap;
}

void UVBMinimapWidget::RefreshVisibility()
{
	const ESlateVisibility Wanted = ShouldShow() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (Wanted == ESlateVisibility::Collapsed)
	{
		DrawRecords.Reset();
		Threats.Reset();
	}

	if (GetVisibility() != Wanted)
	{
		SetVisibility(Wanted);
	}
}

void UVBMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 보이는 동안 폰을 잃으면 여기서 접힌다 - 다시 깨우는 것은 빙의 신호다.
	if (!ShouldShow())
	{
		RefreshVisibility();
		return;
	}

	UpdateFrame(MyGeometry, InDeltaTime);
}

void UVBMinimapWidget::UpdateFrame(const FGeometry& MyGeometry, float DeltaSeconds)
{
	DrawRecords.Reset();
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	MapLocalRadius = 0.5 * FMath::Min(LocalSize.X, LocalSize.Y);
	APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !MapDefinition || MapLocalRadius <= 0.0)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const float MapYaw = static_cast<float>(ViewRotation.Yaw);
	const FVector2D PawnXY(Pawn->GetActorLocation());

	const VBMap::FVBMinimapMaterialParams Params = VBMap::BuildMinimapMaterialParams(
		PawnXY, MapDefinition->WorldMin, MapDefinition->WorldMax, MapYaw, WorldRadiusCm);
	if (!Params.bValid)
	{
		return;
	}

	if (MapMaterialInstance)
	{
		MapMaterialInstance->SetVectorParameterValue(CenterUVParam, ToParam(Params.CenterUV));
		MapMaterialInstance->SetVectorParameterValue(RadiusOverExtentsParam, ToParam(Params.RadiusOverExtents));
		MapMaterialInstance->SetVectorParameterValue(YawCosSinParam, ToParam(Params.YawCosSin));
	}

	// 발견한 저장점: 테두리 밖이면 숨긴다. 발견 여부는 m2a 사본(원격 클라에서도 같은 읽기 API).
	//   발견 목록을 돌며 찾는다 - GetPoints 처럼 매 프레임 새 배열을 만들지 않는다. 아직 안 올라온 지점은 건너뛰고 목록에는 남는다.
	if (const UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this))
	{
		for (const FGameplayTag& PointId : Travel->GetDiscovered())
		{
			if (const AVBSavePoint* Point = Travel->FindPoint(PointId))
			{
				const FVector2D Offset = VBMap::MinimapOffset(PawnXY, FVector2D(Point->GetActorLocation()), MapYaw, WorldRadiusCm);
				AddIcon(EVBMinimapIconKind::SavePoint, Offset, MapLocalRadius);
			}
		}
	}

	// 나와 싸우는 적: 테두리 밖이면 숨긴다(반경 안만). 목표, 플레이어보다 아래에 그린다.
	UpdateThreats(*Pawn, DeltaSeconds);
	for (const FThreat& Threat : Threats)
	{
		const FVector2D Offset = VBMap::MinimapOffset(PawnXY, Threat.LastXY, MapYaw, WorldRadiusCm);
		AddIcon(EVBMinimapIconKind::Threat, Offset, MapLocalRadius, Threat.Opacity);
	}

	// 따라가는 퀘스트의 목표: 테두리 밖이면 테두리 위 화살표. 이 기계에 로드된 마커만 위치가 있다(언로드된 칸의 목표는 안 그린다).
	const UVBQuestLogComponent* Log = QuestLog.Get();
	const FVBQuestLogEntry* Tracked = Log ? Log->GetTrackedEntry() : nullptr;
	const UWorld* World = GetWorld();
	const UVBWorldMarkerSubsystem* Markers = World ? World->GetSubsystem<UVBWorldMarkerSubsystem>() : nullptr;
	if (Tracked && Tracked->Status == EVBQuestStatus::Active && Markers)
	{
		// 목표 마커 층과 같은 세 줄이다(두 번째 사용 - 세 번째가 생기면 VBQuestMarkers 로 뽑는다, key-rules 12).
		//   마커 층의 가까운 셋 상한은 화면 정책이라 따르지 않는다.
		FGameplayTagContainer Tags;
		VBQuestMarkers::CollectActiveMarkerTags(Tracked->Definition, Tracked->StageIndex, Tracked->Progress, Tags);
		TArray<UVBWorldMarkerComponent*> Found;
		Markers->FindMarkers(Tags, Found);
		for (const UVBWorldMarkerComponent* Marker : Found)
		{
			const FVector2D Offset = VBMap::MinimapOffset(PawnXY, FVector2D(Marker->GetComponentLocation()), MapYaw, WorldRadiusCm);
			AddIcon(EVBMinimapIconKind::Objective, Offset, MapLocalRadius);
		}
	}

	// 플레이어는 가운데, 마지막(맨 위). 화살표는 카메라 기준 위에서 폰이 바라보는 쪽으로 돈다.
	FVBMinimapDrawRecord& Player = DrawRecords.AddDefaulted_GetRef();
	Player.Kind = EVBMinimapIconKind::Player;
	Player.AngleDegrees = FRotator::NormalizeAxis(static_cast<float>(Pawn->GetActorRotation().Yaw) - MapYaw);
}

void UVBMinimapWidget::UpdateThreats(const APawn& Pawn, float DeltaSeconds)
{
	for (FThreat& Threat : Threats)
	{
		Threat.bEngaged = false;
	}

	UWorld* World = GetWorld();
	if (UVBEnemyRegistrySubsystem* Registry = World ? World->GetSubsystem<UVBEnemyRegistrySubsystem>() : nullptr)
	{
		for (const TWeakObjectPtr<AVBEnemyBase>& Weak : Registry->GetEnemies())
		{
			AVBEnemyBase* Enemy = Weak.Get();
			const UVBEnemyCombatComponent* Combat = Enemy ? Enemy->GetCombatComponent() : nullptr;
			// 판정은 이 한 줄이다: 살아 있고 전투 대상이 나. 전투 대상은 서버에만 있다(헤더 주석).
			if (!Combat || Enemy->IsDead() || Combat->GetCombatTarget() != &Pawn)
			{
				continue;
			}

			FThreat* Threat = Threats.FindByPredicate([Enemy](const FThreat& Entry) { return Entry.Enemy.Get() == Enemy; });
			if (!Threat)
			{
				Threat = &Threats.AddDefaulted_GetRef();
				Threat->Enemy = Enemy;
			}

			Threat->bEngaged = true;
		}
	}

	for (int32 Index = Threats.Num() - 1; Index >= 0; --Index)
	{
		FThreat& Threat = Threats[Index];
		// 사라지는 중에도 살아 있는 적은 따라간다. 파괴된 적은 마지막 자리에서 사라진다.
		if (const AActor* Enemy = Threat.Enemy.Get())
		{
			Threat.LastXY = FVector2D(Enemy->GetActorLocation());
		}
		// 근처 적 체력 바와 같은 한 걸음(두 번째 사용 - 세 번째가 생기면 뽑는다, key-rules 12).
		const float FadeSeconds = Threat.bEngaged ? ThreatFadeInSeconds : ThreatFadeOutSeconds;
		const float Step = FadeSeconds > KINDA_SMALL_NUMBER ? DeltaSeconds / FadeSeconds : 1.0f;
		Threat.Opacity = FMath::Clamp(Threat.Opacity + (Threat.bEngaged ? Step : -Step), 0.0f, 1.0f);
		if (!Threat.bEngaged && Threat.Opacity <= 0.0f)
		{
			Threats.RemoveAtSwap(Index);
		}
	}
}

void UVBMinimapWidget::AddIcon(EVBMinimapIconKind Kind, const FVector2D& RawOffset, double LocalRadius, float Opacity)
{
	// 목표는 테두리에서 화살표 붓으로 바뀐다 - 둘 중 큰 붓으로 재야 화살표도 테두리 안에 머문다.
	double HalfDiagonal = 0.5 * FVector2D(BrushFor(Kind).ImageSize).Size();
	if (Kind == EVBMinimapIconKind::Objective)
	{
		HalfDiagonal = FMath::Max(HalfDiagonal, 0.5 * FVector2D(ObjectiveRimBrush.ImageSize).Size());
	}

	const VBMap::EVBMinimapIconPolicy Policy = Kind == EVBMinimapIconKind::Objective
		? VBMap::EVBMinimapIconPolicy::ClampToRim
		: VBMap::EVBMinimapIconPolicy::HideOutside;
	const VBMap::FVBMinimapIconPlacement Placement = VBMap::PlaceMinimapIcon(RawOffset, LocalRadius, BorderInset, HalfDiagonal, Policy);
	if (!Placement.bValid || !Placement.bVisible)
	{
		return;
	}

	FVBMinimapDrawRecord& Record = DrawRecords.AddDefaulted_GetRef();
	Record.Kind = Placement.bOnRim ? EVBMinimapIconKind::ObjectiveRim : Kind;
	Record.LocalOffset = Placement.LocalOffset;
	// 화면 가장자리 화살표와 같은 규칙(위를 가리키게 그린 그림을 Direction 으로 돌린다).
	Record.AngleDegrees = Placement.bOnRim ? VBScreenEdge::ArrowAngleDegrees(Placement.Direction) : 0.0f;
	Record.Opacity = Opacity;
}

const FSlateBrush& UVBMinimapWidget::BrushFor(EVBMinimapIconKind Kind) const
{
	switch (Kind)
	{
	case EVBMinimapIconKind::Objective: return ObjectiveBrush;
	case EVBMinimapIconKind::ObjectiveRim: return ObjectiveRimBrush;
	case EVBMinimapIconKind::Player: return PlayerBrush;
	case EVBMinimapIconKind::Threat: return ThreatBrush;
	case EVBMinimapIconKind::SavePoint:
	default: return SavePointBrush;
	}
}

int32 UVBMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 지도(자식)를 먼저 그리고 그 위에 아이콘. 이 함수는 기록만 읽는다 - 등록부를 묻지 않는다.
	int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const int32 IconLayer = MaxLayer + 1;
	const FVector2D Center = FVector2D(AllottedGeometry.GetLocalSize()) * 0.5;
	for (const FVBMinimapDrawRecord& Record : DrawRecords)
	{
		const FSlateBrush& Brush = BrushFor(Record.Kind);
		const FVector2D Size(Brush.ImageSize);
		if (Size.X <= 0.0 || Size.Y <= 0.0)
		{
			continue;
		}

		const FVector2D TopLeft = Center + Record.LocalOffset - 0.5 * Size;
		FSlateDrawElement::MakeRotatedBox(OutDrawElements, IconLayer,
			AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			&Brush, ESlateDrawEffect::None, FMath::DegreesToRadians(Record.AngleDegrees),
			TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement,
			Brush.GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.0f, 1.0f, 1.0f, Record.Opacity));
	}

	return DrawRecords.IsEmpty() ? MaxLayer : IconLayer;
}

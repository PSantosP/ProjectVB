// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Quest/VBObjectiveMarkerLayerWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Map/VBWorldMarkerComponent.h"
#include "Map/VBWorldMarkerSubsystem.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestMarkers.h"
#include "UI/Quest/VBObjectiveMarkerWidget.h"
#include "UI/VBWorldIndicatorProjection.h"
#include "VB/VB.h"

void UVBObjectiveMarkerLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (AVBPlayerController* PC = GetOwningPlayer<AVBPlayerController>())
	{
		PlayerStateSet.Register(*PC, FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBObjectiveMarkerLayerWidget::HandlePlayerStateSet));
	}
	else
	{
		VB_LOG(Warning, "목표 마커 층: 주인이 AVBPlayerController 가 아니다 - 퀘스트 일지를 기다릴 수 없다");
	}
}

void UVBObjectiveMarkerLayerWidget::NativeDestruct()
{
	PlayerStateSet.Unregister();
	QuestLog.Reset();
	Super::NativeDestruct();
}

void UVBObjectiveMarkerLayerWidget::HandlePlayerStateSet(APlayerState* /*PlayerState*/)
{
	QuestLog = UVBQuestLogComponent::FindFor(GetOwningPlayer());
}

void UVBObjectiveMarkerLayerWidget::UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& /*IndicatorSlot*/)
{
	// 이 층의 자리는 표시기 층 전체라 옮기지 않는다. 마커는 자기 캔버스(MarkerCanvas, 역시 전체 화면) 안에 놓는다 -
	//   그래서 View.LocalSize 가 마커 캔버스 크기와 같다(시험 VB.Unit.HUD.WorldIndicatorLayer).
	const APlayerController* PC = View.PC;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	const UVBWorldMarkerSubsystem* Registry = World ? World->GetSubsystem<UVBWorldMarkerSubsystem>() : nullptr;
	if (!PC || !Pawn || !Registry || !MarkerCanvas || !MarkerClass)
	{
		HideFrom(0);
		return;
	}

	const UVBQuestLogComponent* Log = QuestLog.Get();
	const FVBQuestLogEntry* Tracked = Log ? Log->GetTrackedEntry() : nullptr;

	FGameplayTagContainer Tags;
	if (Tracked && Tracked->Status == EVBQuestStatus::Active)
	{
		VBQuestMarkers::CollectActiveMarkerTags(Tracked->Definition, Tracked->StageIndex, Tracked->Progress, Tags);
	}

	TArray<UVBWorldMarkerComponent*> Found;
	Registry->FindMarkers(Tags, Found);
	if (Found.IsEmpty())
	{
		HideFrom(0);
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	Found.Sort([&PawnLocation](const UVBWorldMarkerComponent& A, const UVBWorldMarkerComponent& B)
	{
		return FVector::DistSquared(A.GetComponentLocation(), PawnLocation) < FVector::DistSquared(B.GetComponentLocation(), PawnLocation);
	});

	const int32 Count = FMath::Min(Found.Num(), MaxMarkers);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UVBObjectiveMarkerWidget* Marker = GetOrCreateMarker(Index);
		if (!Marker)
		{
			HideFrom(Index);
			return;
		}

		const FVector Location = Found[Index]->GetComponentLocation() + FVector(0.0f, 0.0f, WorldZOffset);
		const int32 Meters = FMath::RoundToInt(FVector::Dist(Found[Index]->GetComponentLocation(), PawnLocation) / 100.0f);
		// 화면 안이면 마커 위에, 밖(뒤, 배율 무효 포함)이면 가장자리에 화살표와 함께(ClampToEdge).
		const FVBIndicatorPlacement Placement = VBWorldIndicatorProjection::Place(View, Location,
			EVBIndicatorScreenPolicy::ClampToEdge, EdgeMargin);
		PoolSlots[Index]->SetPosition(Placement.Position);
		Marker->SetMarkerState(Placement.bEdge, Placement.bEdge ? Placement.ArrowAngle : 0.0f, Meters);
		if (Marker->GetVisibility() != ESlateVisibility::HitTestInvisible)
		{
			Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	HideFrom(Count);
}

UVBObjectiveMarkerWidget* UVBObjectiveMarkerLayerWidget::GetOrCreateMarker(int32 Index)
{
	while (Pool.Num() <= Index)
	{
		UVBObjectiveMarkerWidget* Marker = CreateWidget<UVBObjectiveMarkerWidget>(GetOwningPlayer(), MarkerClass);
		UCanvasPanelSlot* MarkerSlot = Marker ? MarkerCanvas->AddChildToCanvas(Marker) : nullptr;
		if (!MarkerSlot)
		{
			return nullptr;
		}

		MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MarkerSlot->SetSize(MarkerSize);
		Marker->SetVisibility(ESlateVisibility::Collapsed);
		Pool.Add(Marker);
		PoolSlots.Add(MarkerSlot);
	}

	return Pool[Index];
}

void UVBObjectiveMarkerLayerWidget::HideFrom(int32 Index)
{
	for (int32 Hide = Index; Hide < Pool.Num(); ++Hide)
	{
		if (Pool[Hide] && Pool[Hide]->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Pool[Hide]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

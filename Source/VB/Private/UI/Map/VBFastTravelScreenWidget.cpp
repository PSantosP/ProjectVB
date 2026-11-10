// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Map/VBFastTravelScreenWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/Progress/VBProgressCatalogSubsystem.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "UI/VBScreenText.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBFastTravelScreen"

UVBFastTravelScreenWidget::UVBFastTravelScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 뒤로 가기를 받아 닫기 요청으로 돌린다. 끄면 뒤로 가기로 목록을 벗어날 길이 없다.
	bIsBackHandler = true;
	bRequireCloseButton = true;
	RegionFormat = LOCTEXT("Region", "{0}");
	ResultTexts.Add(EVBFastTravelResult::UnknownDestination, LOCTEXT("Unknown", "갈 수 없는 곳이다"));
	ResultTexts.Add(EVBFastTravelResult::SamePoint, LOCTEXT("Same", "지금 있는 곳이다"));
}

void UVBFastTravelScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnTravel)
	{
		BtnTravel->OnClicked.AddUniqueDynamic(this, &UVBFastTravelScreenWidget::HandleTravelClicked);
	}
}

void UVBFastTravelScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	const APlayerController* PC = GetOwningPlayer();
	UVBFastTravelComponent* Found = PC ? PC->FindComponentByClass<UVBFastTravelComponent>() : nullptr;
	FastTravel.Bind(Found, &UVBFastTravelComponent::OnViewChanged, this, &UVBFastTravelScreenWidget::Rebuild);
	VB_CLOG(!Found, Warning, "빠른 이동 화면: 이 플레이어의 빠른 이동 컴포넌트를 찾지 못했다(PlayerController 에 UVBFastTravelComponent)");
	SelectedPoint = FGameplayTag();
	Rebuild();
}

void UVBFastTravelScreenWidget::NativeOnDeactivated()
{
	FastTravel.Reset();
	Super::NativeOnDeactivated();
}

bool UVBFastTravelScreenWidget::NativeOnHandleBackAction()
{
	// 이 값은 CommonUI 의 입력 바인딩만 본다. BtnClose 는 HandleBackAction 으로 바로 들어온다.
	if (!bIsBackHandler)
	{
		return false;
	}

	RequestCloseTravel();
	return true;
}

void UVBFastTravelScreenWidget::RequestCloseTravel()
{
	if (UVBFastTravelComponent* Owned = FastTravel.Get())
	{
		Owned->RequestClose();
	}
	else
	{
		// 빠른 이동 컴포넌트를 못 찾았다 - 화면만이라도 내려 입력이 갇히지 않게 한다.
		DeactivateWidget();
	}
}

FText UVBFastTravelScreenWidget::RegionName(const FGameplayTag& RegionId) const
{
	const UVBProgressCatalogSubsystem* Catalog = UVBProgressCatalogSubsystem::Get(this);
	const UVBRegionDefinition* Region = Catalog ? Catalog->FindRegion(RegionId) : nullptr;
	return Region ? Region->DisplayName : FText::GetEmpty();
}

int32 UVBFastTravelScreenWidget::BuildRows()
{
	RowPoints.Reset();
	const UVBFastTravelComponent* Owned = FastTravel.Get();
	const FVBFastTravelView* View = Owned ? &Owned->GetView() : nullptr;
	const int32 EntryCount = View ? View->Entries.Num() : 0;
	for (int32 Index = 0; Index < EntryCount; ++Index)
	{
		const FVBFastTravelEntry& Entry = View->Entries[Index];
		AddRow(RowPoints.Add(Entry.PointId), Entry.DisplayName, FText::Format(RegionFormat, RegionName(Entry.RegionId)));
	}

	KeepSelectionOrFirst(RowPoints, SelectedPoint, FGameplayTag());
	return RowPoints.Num();
}

void UVBFastTravelScreenWidget::Select(int32 RowIndex)
{
	if (RowPoints.IsValidIndex(RowIndex))
	{
		SelectedPoint = RowPoints[RowIndex];
		RefreshDetail();
	}
}

TArray<UWidget*> UVBFastTravelScreenWidget::GetDetailActions() const
{
	return { BtnTravel };
}

void UVBFastTravelScreenWidget::RefreshDetail()
{
	const UVBFastTravelComponent* Owned = FastTravel.Get();
	const FVBFastTravelEntry* Entry = Owned
		? Owned->GetView().Entries.FindByPredicate([this](const FVBFastTravelEntry& E) { return E.PointId == SelectedPoint; })
		: nullptr;
	if (TxtDestination)
	{
		TxtDestination->SetText(Entry ? Entry->DisplayName : FText::GetEmpty());
	}

	const EVBFastTravelResult Result = Owned && Entry ? Owned->PreviewTravel(SelectedPoint) : EVBFastTravelResult::NotOpen;
	if (BtnTravel)
	{
		BtnTravel->SetIsEnabled(Result == EVBFastTravelResult::Ok);
	}

	if (TxtStatus)
	{
		TxtStatus->SetText(VBScreenText::ResultReason(ResultTexts, Result, Entry != nullptr));
	}
}

void UVBFastTravelScreenWidget::HandleTravelClicked()
{
	if (UVBFastTravelComponent* Owned = FastTravel.Get(); Owned && SelectedPoint.IsValid())
	{
		Owned->RequestTravel(SelectedPoint);
	}
}

#undef LOCTEXT_NAMESPACE

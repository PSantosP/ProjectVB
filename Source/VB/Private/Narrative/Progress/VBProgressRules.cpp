// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Progress/VBProgressRules.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Narrative/Progress/VBProgressCatalogSubsystem.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerController.h"
#include "VB/VB.h"

void FVBEffect_EnterRegion::Apply(const FVBRuleContext& Context) const
{
	if (!Context.CanApplyEffects())
	{
		return;
	}

	const UVBProgressCatalogSubsystem* Catalog = Context.World ? UVBProgressCatalogSubsystem::Get(Context.World) : nullptr;
	const UVBRegionDefinition* Region = Catalog ? Catalog->FindRegion(RegionId) : nullptr;
	AVBPlayerController* Controller = Context.Instigator ? Cast<AVBPlayerController>(Context.Instigator->GetPlayerController()) : nullptr;
	if (!Region || !Controller)
	{
		VB_LOG(Warning, "효과(지역 들어옴): %s - %s", *RegionId.ToString(), !Region ? TEXT("지역 정의가 카탈로그에 없다") : TEXT("들어온 플레이어의 컨트롤러가 없다"));
		return;
	}

	bool bFirstVisit = true;
	if (Region->VisitedFact.IsValid() && Context.Story)
	{
		bFirstVisit = !Context.Story->HasFact(Region->VisitedFact);
		if (bFirstVisit)
		{
			Context.Story->SetFact(Region->VisitedFact, true);
		}
	}

	Controller->ClientRegionEntered(RegionId, bFirstVisit);
}

#if !UE_BUILD_SHIPPING
// 개발용: 영역 트리거 없이 '지역 들어옴' 효과를 로컬(첫) 플레이어로 한 번 돌린다 - 배너, 가 봄 사실을 레벨 배선 전에 확인한다.
//   효과 그 자체를 부르므로 서버, 싱글에서만 돈다(FVBRuleContext::CanApplyEffects).
static void HandleEnterRegionCommand(const TArray<FString>& Args, UWorld* World)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	FVBEffect_EnterRegion Effect;
	Effect.RegionId = Args.Num() > 0 ? FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false) : FGameplayTag();
	if (!PC || !PC->PlayerState || !Effect.RegionId.IsValid())
	{
		VB_LOG(Warning, "VB.Region.Enter: %s", !Effect.RegionId.IsValid() ? TEXT("등록되지 않은 지역 태그다") : TEXT("로컬 플레이어가 없다"));
		return;
	}

	Effect.Apply(FVBRuleContext::Make(World, PC->PlayerState));
}

static FAutoConsoleCommandWithWorldAndArgs GVBEnterRegionCommand(
	TEXT("VB.Region.Enter"),
	TEXT("개발용: 지역 들어옴 효과를 첫 로컬 플레이어로 돌린다. 인자 = 지역 태그(예: Map.Region.Test.Ward)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleEnterRegionCommand));
#endif

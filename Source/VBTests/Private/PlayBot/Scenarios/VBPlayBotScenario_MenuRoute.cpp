// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "GameFramework/Character.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

// 메뉴 경로 하나를 실제 패드 입력으로 끝까지 누른다. 준비로 아이템을 넣고(+Grant=<아이템 자산>|<개수>), +Route 의 Press/Seek 를 차례로 한다.
//   경로가 끝까지 가면 MenuRoute.Completed 통과 - 패드로 닿지 않는 버튼은 Seek 가 「N번 눌러도 안 보인다」로 떨어진다.
class FVBPlayBotScenario_MenuRoute : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		const TArray<FString> Route = C.GetArray(TEXT("Route"));
		if (Route.IsEmpty())
		{
			OutError = FString::Printf(TEXT("[%s] needs +Route=<menu step> lines"), *C.Section);
			return false;
		}

		for (const FString& Line : C.GetArray(TEXT("Grant")))
		{
			FString Path;
			FString CountText;
			const UVBItemDefinition* Item = Line.Split(TEXT("|"), &Path, &CountText)
				? LoadObject<UVBItemDefinition>(nullptr, *Path) : nullptr;
			if (!Item || !CountText.IsNumeric() || FCString::Atoi(*CountText) <= 0)
			{
				OutError = FString::Printf(TEXT("Grant '%s' must be <item asset>|<count> with a loadable item"), *Line);
				return false;
			}

			Grants.Emplace(Item, FCString::Atoi(*CountText));
		}

		Sequence.Then<FVBSkillDo>(TEXT("grant items"), [this](FVBPlayBotContext& C2, FString& Error)
		{
			UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(C2.Player);
			if (!Grants.IsEmpty() && !Inventory)
			{
				Error = TEXT("player has no inventory component");
				return false;
			}

			for (const TPair<TWeakObjectPtr<const UVBItemDefinition>, int32>& Grant : Grants)
			{
				if (!Grant.Key.IsValid() || Inventory->AddItem(Grant.Key.Get(), Grant.Value) != Grant.Value)
				{
					Error = FString::Printf(TEXT("inventory refused %s"), *GetNameSafe(Grant.Key.Get()));
					return false;
				}
			}

			return true;
		});
		if (!VBPlayBotSkills::AddMenuPath(Sequence, Route, TMap<FString, FString>(), OutError))
		{
			return false;
		}

		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			Ctx.Checks->Expect(TEXT("MenuRoute.Completed"), false, true, Ctx.Frame,
				FString::Printf(TEXT("{\"reason\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
			return Fail(Sequence.GetFailure());
		}

		if (State == EVBPlayBotState::Succeeded)
		{
			Ctx.Checks->Expect(TEXT("MenuRoute.Completed"), true, true, Ctx.Frame);
		}

		return State;
	}

private:
	FVBPlayBotSequence Sequence;
	TArray<TPair<TWeakObjectPtr<const UVBItemDefinition>, int32>> Grants;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_MenuRoute, "MenuRoute")

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Character.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "UI/VBBossHealthBarWidget.h"
#include "Widgets/SViewport.h"

// 보스와 싸워 넘어뜨린 뒤 그 뒤로 걸어간다.
// PASS = 넘어뜨린 때부터 끝까지 모든 프레임에서 보스 바가 보이고 화면 아래쪽에 머문다.
class FVBPlayBotScenario_BossBar : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString BossClass, Weapon, Slot, AttackText;
		if (!C.RequireString(TEXT("BossClass"), BossClass, OutError) || !C.RequireString(TEXT("Weapon"), Weapon, OutError)
			|| !C.RequireString(TEXT("WeaponAction"), Slot, OutError) || !C.RequireString(TEXT("AttackAction"), AttackText,
				OutError))
		{
			return false;
		}

		const double SearchRadius = C.GetDouble(TEXT("SearchRadius"), 4000.0);
		const double Range = C.GetDouble(TEXT("AttackRange"), 220.0);
		const double KnockdownSeconds = C.GetDouble(TEXT("KnockdownSeconds"), 45.0);
		const double BehindSeconds = C.GetDouble(TEXT("BehindSeconds"), 3.0);
		BottomFraction = C.GetDouble(TEXT("BottomFraction"), 0.6);

		Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
		Sequence.Then<FVBSkillDo>(TEXT("find boss"), [this, BossClass, SearchRadius](FVBPlayBotContext& C2, FString& Error)
		{
			ACharacter* Found = Cast<ACharacter>(C2.FindNearest(FSoftClassPath(BossClass).TryLoadClass<ACharacter>(), NAME_None,
				SearchRadius));
			if (!Found)
			{
				Error = FString::Printf(TEXT("no %s within %.0f cm"), *BossClass, SearchRadius);
				return false;
			}

			Boss = Found;
			C2.Target = Found;
			C2.Watch(Found);
			return true;
		});
		auto GetBoss = [this](FVBPlayBotContext&) { return Boss.Get(); };
		Sequence.Then<FVBSkillAttackUntil>(TEXT("fight until knockdown"), GetBoss, FName(*AttackText), Range,
			[this](FVBPlayBotContext& C2, int32) { return IsDowned(); }, KnockdownSeconds);
		Sequence.Then<FVBSkillDo>(TEXT("knockdown seen"), [this](FVBPlayBotContext& C2, FString&) { WatchFrom = C2.Frame; return true; });
		// 넘어진 적을 도는 플레이어처럼 보스의 반대편으로 걸어간다.
		Sequence.Then<FVBSkillMove>(TEXT("walk behind the boss"), [this](FVBPlayBotContext& C2)
		{
			const AActor* B = Boss.Get();
			if (!B)
			{
				return FVector::ZeroVector;
			}

			const FVector Behind = B->GetActorLocation() - B->GetActorForwardVector() * 250.0;
			return Behind - C2.Player->GetActorLocation();
		}, BehindSeconds);
		Sequence.Then<FVBSkillWait>(TEXT("hold"), 1.0);
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (WatchFrom >= 0)
		{
			SampleBar(Ctx);
		}

		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		if (State == EVBPlayBotState::Failed && WatchFrom < 0)
		{
			Ctx.Checks->Expect(TEXT("boss_knocked_down"), false, true, Ctx.Frame, FString::Printf(TEXT("{\"why\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
			return EVBPlayBotState::Succeeded;
		}

		if (State == EVBPlayBotState::Failed)
		{
			return Fail(Sequence.GetFailure());
		}

		const FString Evidence = FString::Printf(TEXT("{\"frames\":%d,\"measured\":%d,\"unmeasured\":%d,\"hiddenFrames\":%d,\"firstHidden\":%lld,\"highFrames\":%d,\"firstHigh\":%lld,\"minY\":%.3f}"),
			Frames, MeasuredFrames, UnmeasuredFrames, HiddenFrames, FirstHidden, HighFrames, FirstHigh, MinY);
		Ctx.Checks->Expect(TEXT("boss_knocked_down"), true, true, WatchFrom);
		Ctx.Checks->Expect(TEXT("boss_bar_visible_through_knockdown"), Frames > 0 && HiddenFrames == 0, true,
			FirstHidden >= 0 ? FirstHidden : WatchFrom, Evidence);
		if (UnmeasuredFrames > 0 || MeasuredFrames == 0)
		{
			Ctx.Checks->Unavailable(TEXT("boss_bar_stays_at_bottom"), WatchFrom, TEXT("bar position could not be measured on every frame"));
		}
		else
		{
			Ctx.Checks->Expect(TEXT("boss_bar_stays_at_bottom"), HighFrames == 0, true, FirstHigh >= 0 ? FirstHigh : WatchFrom, Evidence);
		}

		Ctx.Checks->Look(TEXT("boss_bar"), WatchFrom - 30, Ctx.Frame, TEXT("bar during knockdown and walking behind"));
		return EVBPlayBotState::Succeeded;
	}

private:
	bool IsDowned() const
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Boss.Get());
		return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Downed);
	}

	void SampleBar(FVBPlayBotContext& Ctx)
	{
		++Frames;
		UUserWidget* Bar = Ctx.FindWidget(UVBBossHealthBarWidget::StaticClass());
		const bool bVisible = Bar && Bar->IsVisible() && Bar->GetCachedGeometry().GetLocalSize().Y > 0.f;
		if (!bVisible)
		{
			++HiddenFrames;
			if (FirstHidden < 0)
			{
				FirstHidden = Ctx.Frame;
			}

			return;
		}
		// viewport 공간의 바 중심: 0 = 위 끝, 1 = 아래 끝.
		const TSharedPtr<SViewport> Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
		if (!Viewport.IsValid())
		{
			// 건너뛰지 않고 센다 - 위치를 못 잰 프레임이 "아래에 머묾" 으로 읽히면 안 된다.
			++UnmeasuredFrames;
			return;
		}

		++MeasuredFrames;
		const FGeometry& View = Viewport->GetCachedGeometry();
		const FGeometry& Geo = Bar->GetCachedGeometry();
		const double Centre = Geo.GetAbsolutePosition().Y + Geo.GetAbsoluteSize().Y * 0.5;
		const double Y = (Centre - View.GetAbsolutePosition().Y) / FMath::Max(1.0, static_cast<double>(View.GetAbsoluteSize().Y));
		MinY = FMath::Min(MinY, Y);
		if (Y < BottomFraction)
		{
			++HighFrames;
			if (FirstHigh < 0)
			{
				FirstHigh = Ctx.Frame;
			}
		}
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Boss;
	double BottomFraction = 0.6;
	int64 WatchFrom = -1;
	int32 Frames = 0;
	int32 HiddenFrames = 0;
	int32 HighFrames = 0;
	int32 MeasuredFrames = 0;
	int32 UnmeasuredFrames = 0;
	int64 FirstHidden = -1;
	int64 FirstHigh = -1;
	double MinY = 1.0;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_BossBar, "BossBar")

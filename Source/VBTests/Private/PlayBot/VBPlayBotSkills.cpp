// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotSkills.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "CommonInputSettings.h"
#include "CommonInputSubsystem.h"
#include "CommonUITypes.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "ICommonInputModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Widgets/Text/STextBlock.h"
#include "Character/VBCharacter.h"
#include "Engine/World.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Character.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "Types/VBWeaponStateTypes.h"
#include "UI/VBUISettings.h"

namespace VBPlayBotSkills
{
	bool TurnCameraToward(FVBPlayBotContext& Ctx, const AActor* Target, double AlignedDeg, double YawOffsetDeg)
	{
		if (!Target || !Ctx.PC || !Ctx.Player)
		{
			return false;
		}

		const double Want = (Target->GetActorLocation() - Ctx.Player->GetActorLocation()).Rotation().Yaw + YawOffsetDeg;
		const double Delta = FMath::FindDeltaAngleDegrees(Ctx.PC->GetControlRotation().Yaw, Want);
		// 오른쪽 스틱 X > 0 이면 오른쪽으로 돈다 (UVBHeroComponent::HandleLook -> AddControllerYawInput). 목표 근처에서는 늦춘다.
		const double Abs = FMath::Abs(Delta);
		const double Stick = Abs <= AlignedDeg * 0.5 ? 0.0 : FMath::Sign(Delta) * FMath::Clamp(Abs / 60.0, 0.35, 1.0);
		Ctx.Input->SetLookWant(FVector2D(Stick, 0.0));
		return Abs <= AlignedDeg;
	}

	bool EnsureAction(FVBPlayBotContext& Ctx, FName Action, FString& OutError)
	{
		return Ctx.Input->ResolveAction(Ctx.PC, Action, Ctx.FindAction(Action), OutError);
	}

	int32 ReactPress(FVBPlayBotContext& Ctx, FName Action, const FKey& RawKey, const FString& Cue, int64 CueFrame, double HoldSeconds)
	{
		FVBPlayBotPress Press;
		Press.Action = Action;
		Press.Key = RawKey;
		Press.Cue = Cue;
		Press.CueFrame = CueFrame;
		Press.ObservedFrame = CueFrame + Ctx.Human->GetProfile().PresentationFrames;
		Press.DelayMs = Ctx.Human->SampleSimpleReactionMs();
		Press.DueFrame = Press.ObservedFrame + Ctx.MsToFrames(Press.DelayMs);
		const double HoldMs = HoldSeconds > 0.0 ? HoldSeconds * 1000.0 : Ctx.Human->SampleHoldMs();
		Press.HoldFrames = FMath::Max<int32>(1, static_cast<int32>(Ctx.MsToFrames(HoldMs)));
		return Ctx.Input->Schedule(MoveTemp(Press));
	}

	namespace
	{
		// Focus 를 주면 그 아래가 아닌 다른 포커스 가능 위젯의 글은 건너뛴다 - 옆 버튼의 이름표는 그 버튼의 것이다.
		//   시작 위젯 자신은 거르지 않는다.
		void CollectTexts(const TSharedRef<SWidget>& Widget, TArray<FString>& Out, int32 Depth, const SWidget* Focus = nullptr)
		{
			if (Depth > 12)
			{
				return;
			}

			if (Focus && Depth > 0 && &Widget.Get() != Focus && Widget->SupportsKeyboardFocus())
			{
				return;
			}

			if (Widget->GetType() == FName(TEXT("STextBlock")))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
				if (!Text.IsEmpty())
				{
					Out.AddUnique(Text);
				}
			}

			if (FChildren* Children = Widget->GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					CollectTexts(Children->GetChildAt(Index), Out, Depth + 1, Focus);
				}
			}
		}
	}

	namespace
	{
		int32 CountFocusable(const TSharedRef<SWidget>& Widget, int32 Depth)
		{
			if (Depth > 12)
			{
				return 0;
			}

			int32 Count = Widget->SupportsKeyboardFocus() ? 1 : 0;
			if (FChildren* Children = Widget->GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					Count += CountFocusable(Children->GetChildAt(Index), Depth + 1);
				}
			}

			return Count;
		}
	}

	namespace
	{
		struct FScreenText
		{
			FString Text;
			FVector2D Centre;
		};

		void CollectVisibleTexts(const TSharedRef<SWidget>& Widget, TArray<FScreenText>& Out, int32 Depth)
		{
			if (Depth > 40 || !Widget->GetVisibility().IsVisible())
			{
				return;
			}

			if (Widget->GetType() == FName(TEXT("STextBlock")))
			{
				const FGeometry& Geo = Widget->GetCachedGeometry();
				const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
				if (Geo.GetLocalSize().X > 0.f && !Text.IsEmpty()) Out.Add({ Text, FVector2D(Geo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f))) });
			}

			if (FChildren* Children = Widget->GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					CollectVisibleTexts(Children->GetChildAt(Index), Out, Depth + 1);
				}
			}
		}
	}

	bool FindRowTarget(const TSharedRef<SWindow>& Window, const FString& Spec, FVector2D& OutCentre, TArray<FString>* OutRowTexts)
	{
		TArray<FScreenText> Texts;
		CollectVisibleTexts(Window, Texts, 0);
		FString Target = Spec;
		FString Anchor;
		if (!Spec.Split(TEXT("@"), &Target, &Anchor))
		{
			const FScreenText* Found = Texts.FindByPredicate([&Spec](const FScreenText& T) { return T.Text.Contains(Spec); });
			if (Found)
			{
				OutCentre = Found->Centre;
			}

			return Found != nullptr;
		}
		// "Target@Anchor" - Anchor 와 같은 줄(세로 18 px 안)에서 Anchor 에 가장 가까운 Target 글을 찾는다.
		const FScreenText* Row = Texts.FindByPredicate([&Anchor](const FScreenText& T) { return T.Text.Contains(Anchor); });
		if (!Row)
		{
			return false;
		}

		const FScreenText* Best = nullptr;
		for (const FScreenText& T : Texts)
		{
			if (FMath::Abs(T.Centre.Y - Row->Centre.Y) > 18.0)
			{
				continue;
			}

			if (OutRowTexts)
			{
				OutRowTexts->Add(T.Text);
			}

			if (T.Text == Target && (!Best || FMath::Abs(T.Centre.X - Row->Centre.X) < FMath::Abs(Best->Centre.X - Row->Centre.X)))
			{
				Best = &T;
			}
		}

		if (Best)
		{
			OutCentre = Best->Centre;
		}

		return Best != nullptr;
	}

	namespace
	{
		// FindVisibleText - Widget 아래에서 Labels 중 하나를 담은 첫 보이는 STextBlock 의 화면 중심을 OutCentre 에 넣는다.
		bool FindVisibleText(const TSharedRef<SWidget>& Widget, const TArray<FString>& Labels, FVector2D& OutCentre, int32 Depth)
		{
			if (Depth > 40 || !Widget->GetVisibility().IsVisible())
			{
				return false;
			}

			if (Widget->GetType() == FName(TEXT("STextBlock")))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
				const FGeometry& Geo = Widget->GetCachedGeometry();
				if (Geo.GetLocalSize().X > 0.f && Labels.ContainsByPredicate([&Text](const FString& L) { return Text.Contains(L); }))
				{
					OutCentre = Geo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
					return true;
				}
			}

			if (FChildren* Children = Widget->GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					if (FindVisibleText(Children->GetChildAt(Index), Labels, OutCentre, Depth + 1))
					{
						return true;
					}
				}
			}

			return false;
		}
	}

	TArray<FString> FocusTexts()
	{
		TArray<FString> Texts;
		if (!FSlateApplication::IsInitialized())
		{
			return Texts;
		}

		const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetUserFocusedWidget(0);
		if (!Focus.IsValid())
		{
			return Texts;
		}

		CollectTexts(Focus.ToSharedRef(), Texts, 0);
		// 포커스된 줄 버튼은 이름표를 옆에 둔다. 조상이 아직 한 줄일 때만 올라간다 -
		// 메뉴 목록 전체를 읽으면 다른 항목 글까지 섞여 엉뚱한 항목이 강조된 것처럼 보인다.
		TSharedPtr<SWidget> Ancestor = Focus->GetParentWidget();
		for (int32 Level = 0; Level < 3 && Ancestor.IsValid(); ++Level, Ancestor = Ancestor->GetParentWidget())
		{
			if (CountFocusable(Ancestor.ToSharedRef(), 0) > 3)
			{
				break;
			}

			CollectTexts(Ancestor.ToSharedRef(), Texts, 0, Focus.Get());
		}

		return Texts;
	}

	bool CommonUIKey(FVBPlayBotContext& Ctx, bool bBack, FKey& OutKey, FString& OutError)
	{
		const UCommonInputSettings& Settings = ICommonInputModule::GetSettings();
		if (const UInputAction* Action = bBack ? Settings.GetEnhancedInputBackAction() : Settings.GetEnhancedInputClickAction())
		{
			const FName Name = bBack ? FName(TEXT("CommonUI.Back")) : FName(TEXT("CommonUI.Accept"));
			// 확인/뒤로는 메뉴가 열릴 때만 붙는 UI 맵(UVBUISettings::UIInputMapping)에 있다 - 게임 입력 맵에서 찾으면 영영 없다.
			const UInputMappingContext* UIContext = GetDefault<UVBUISettings>()->UIInputMapping.LoadSynchronous();
			if (!Ctx.Input->ResolveActionInContext(Name, Action, UIContext, OutError))
			{
				return false;
			}

			OutKey = Ctx.Input->GetKey(Name);
			return true;
		}

		const FDataTableRowHandle Handle = bBack ? Settings.GetDefaultBackAction() : Settings.GetDefaultClickAction();
		const FCommonInputActionDataBase* Row = Handle.GetRow<FCommonInputActionDataBase>(TEXT("PlayBot"));
		const UCommonInputSubsystem* CommonInput = Ctx.PC ? UCommonInputSubsystem::Get(Ctx.PC->GetLocalPlayer()) : nullptr;
		if (!Row || !CommonInput)
		{
			OutError = FString::Printf(TEXT("CommonUI default %s action is not set"), bBack ? TEXT("back") : TEXT("click"));
			return false;
		}

		OutKey = Row->GetInputTypeInfo(ECommonInputType::Gamepad, CommonInput->GetCurrentGamepadName()).GetKey();
		if (!OutKey.IsValid())
		{
			OutError = FString::Printf(TEXT("CommonUI default %s action has no gamepad key"), bBack ? TEXT("back") : TEXT("click"));
			return false;
		}

		return true;
	}

	bool AddMenuPath(FVBPlayBotSequence& Sequence, const TArray<FString>& Lines, const TMap<FString, FString>& Vars, FString& OutError)
	{
		for (FString Line : Lines)
		{
			for (const TPair<FString, FString>& Var : Vars)
			{
				Line.ReplaceInline(*FString::Printf(TEXT("$%s"), *Var.Key), *Var.Value);
			}

			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT("|"), /*CullEmpty=*/false);
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
			}

			const FString Command = Parts.IsValidIndex(0) ? Parts[0] : FString();
			auto Bad = [&OutError, &Line](const TCHAR* Why) { OutError = FString::Printf(TEXT("menu line '%s': %s"), *Line, Why); return false; };

			if (Command == TEXT("Seek"))
			{
				if (Parts.Num() != 4)
				{
					return Bad(TEXT("Seek needs labels|key|max"));
				}

				TArray<FString> Labels;
				Parts[1].ParseIntoArray(Labels, TEXT(","), /*CullEmpty=*/true);
				if (Labels.IsEmpty())
				{
					return Bad(TEXT("no label"));
				}

				if (Parts[2] == TEXT("Accept") || Parts[2] == TEXT("Back"))
				{
					const bool bBack = Parts[2] == TEXT("Back");
					Sequence.Add(MakeUnique<FVBSkillMenuSeek>(TEXT("seek ") + Parts[1], MoveTemp(Labels),
						[bBack](FVBPlayBotContext& Ctx, FKey& Key, FString& Error) { return CommonUIKey(Ctx, bBack, Key, Error); }, FCString::Atoi(*Parts[3])));
				}
				else
				{
					const FKey Key(*Parts[2]);
					if (!Key.IsValid())
					{
						return Bad(TEXT("unknown key"));
					}

					Sequence.Then<FVBSkillMenuSeek>(TEXT("seek ") + Parts[1], MoveTemp(Labels), Key, FCString::Atoi(*Parts[3]));
				}
			}
			else if (Command == TEXT("Click"))
			{
				if (Parts.Num() != 3)
				{
					return Bad(TEXT("Click needs labels|seconds"));
				}

				TArray<FString> Labels;
				Parts[1].ParseIntoArray(Labels, TEXT(","), /*CullEmpty=*/true);
				if (Labels.IsEmpty())
				{
					return Bad(TEXT("no label"));
				}

				Sequence.Then<FVBSkillMenuClick>(TEXT("click ") + Parts[1], MoveTemp(Labels), FCString::Atod(*Parts[2]));
			}
			else if (Command == TEXT("ClickUntil"))
			{
				// ClickUntil|<target@anchor>|<anchor 줄에 나와야 할 글>|<최대 클릭 수>
				if (Parts.Num() != 4)
				{
					return Bad(TEXT("ClickUntil needs target@anchor|until|max"));
				}

				Sequence.Then<FVBSkillMenuClick>(TEXT("click ") + Parts[1] + TEXT(" until ") + Parts[2], TArray<FString>{ Parts[1] }, 5.0, Parts[2], FCString::Atoi(*Parts[3]));
			}
			else if (Command == TEXT("Steer"))
			{
				if (Parts.Num() != 3)
				{
					return Bad(TEXT("Steer needs labels|max"));
				}

				TArray<FString> Labels;
				Parts[1].ParseIntoArray(Labels, TEXT(","), /*CullEmpty=*/true);
				if (Labels.IsEmpty())
				{
					return Bad(TEXT("no label"));
				}

				Sequence.Then<FVBSkillMenuSteer>(TEXT("steer ") + Parts[1], MoveTemp(Labels), FCString::Atoi(*Parts[2]));
			}
			else if (Command == TEXT("Press"))
			{
				if (Parts.Num() != 2)
				{
					return Bad(TEXT("Press needs one target"));
				}

				const FString Target = Parts[1];
				if (Target == TEXT("Accept") || Target == TEXT("Back"))
				{
					const bool bBack = Target == TEXT("Back");
					Sequence.Then<FVBSkillPressResolved>(TEXT("press ") + Target, [bBack](FVBPlayBotContext& Ctx, FKey& Key, FString& Error)
					{
						return CommonUIKey(Ctx, bBack, Key, Error);
					});
				}
				else if (Target.StartsWith(TEXT("Key=")))
				{
					const FKey Key(*Target.Mid(4));
					if (!Key.IsValid())
					{
						return Bad(TEXT("unknown key"));
					}

					Sequence.Then<FVBSkillPress>(TEXT("press ") + Key.ToString(), Key);
				}
				else if (Target.StartsWith(TEXT("Action=")))
				{
					Sequence.Then<FVBSkillPress>(TEXT("press ") + Target, FName(*Target.Mid(7)));
				}
				else if (Target.StartsWith(TEXT("WidgetAction=")))
				{
					FString ClassPath;
					FString Property;
					if (!Target.Mid(13).Split(TEXT(":"), &ClassPath, &Property, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
					{
						return Bad(TEXT("WidgetAction needs class:property"));
					}

					Sequence.Then<FVBSkillPressResolved>(TEXT("press ") + Property,
						[ClassPath, Property](FVBPlayBotContext& Ctx, FKey& Key, FString& Error)
					{
						UClass* Class = FSoftClassPath(ClassPath).TryLoadClass<UUserWidget>();
						UUserWidget* Widget = Ctx.FindWidget(Class);
						const FObjectProperty* Prop = Widget ? FindFProperty<FObjectProperty>(Widget->GetClass(),
							FName(*Property)) : nullptr;
						const UInputAction* Action = Prop ? Cast<UInputAction>(Prop->GetObjectPropertyValue_InContainer(Widget)) : nullptr;
						if (!Action)
						{
							Error = FString::Printf(TEXT("%s.%s not found on a live widget"), *ClassPath, *Property);
							return false;
						}

						if (!Ctx.Input->ResolveAction(Ctx.PC, FName(*Property), Action, Error))
						{
							return false;
						}

						Key = Ctx.Input->GetKey(FName(*Property));
						return true;
					});
				}
				else
				{
					return Bad(TEXT("unknown press target"));
				}
			}
			else if (Command == TEXT("WaitWidget") || Command == TEXT("WaitNoWidget"))
			{
				if (Parts.Num() != 3)
				{
					return Bad(TEXT("needs class|seconds"));
				}

				const bool bWantPresent = Command == TEXT("WaitWidget");
				const FString ClassPath = Parts[1];
				Sequence.Then<FVBSkillWaitUntil>(Command + TEXT(" ") + ClassPath, [ClassPath, bWantPresent](FVBPlayBotContext& Ctx)
				{
					return (Ctx.FindWidget(FSoftClassPath(ClassPath).TryLoadClass<UUserWidget>()) != nullptr) == bWantPresent;
				}, FCString::Atod(*Parts[2]));
			}
			else if (Command == TEXT("PadMode"))
			{
				if (Parts.Num() != 2)
				{
					return Bad(TEXT("PadMode needs seconds"));
				}

				Sequence.Then<FVBSkillPadMode>(FCString::Atod(*Parts[1]));
			}
			else if (Command == TEXT("Wait"))
			{
				if (Parts.Num() != 2)
				{
					return Bad(TEXT("Wait needs seconds"));
				}

				Sequence.Then<FVBSkillWait>(TEXT("wait"), FCString::Atod(*Parts[1]));
			}
			else
			{
				return Bad(TEXT("unknown command"));
			}
		}

		return true;
	}

	bool StopAI(APawn* Pawn)
	{
		const AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		UBrainComponent* Brain = AI ? AI->GetBrainComponent() : nullptr;
		if (Brain && Brain->IsRunning())
		{
			Brain->StopLogic(TEXT("PlayBot"));
		}

		return !Brain || !Brain->IsRunning();
	}

	ACharacter* SpawnStillTarget(FVBPlayBotContext& Ctx, const FString& ClassPath, double Distance, FString& OutError)
	{
		UClass* Class = FSoftClassPath(ClassPath).TryLoadClass<ACharacter>();
		if (!Class)
		{
			OutError = FString::Printf(TEXT("%s did not load as a Character"), *ClassPath);
			return nullptr;
		}

		const FVector Forward = Ctx.Player->GetActorForwardVector().GetSafeNormal2D();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACharacter* Spawned = Ctx.World->SpawnActor<ACharacter>(Class, Ctx.Player->GetActorLocation() + Forward * Distance,
			(-Forward).Rotation(), Params);
		if (!Spawned)
		{
			OutError = TEXT("target spawn failed");
			return nullptr;
		}

		if (!Spawned->GetController())
		{
			Spawned->SpawnDefaultController();
		}

		StopAI(Spawned);
		return Spawned;
	}
}

FVBPlayBotSequence& FVBPlayBotSequence::Add(TUniquePtr<FVBPlayBotSkill> Skill)
{
	Steps.Add(MoveTemp(Skill));
	return *this;
}

void FVBPlayBotSequence::Reset()
{
	Steps.Reset();
	Index = 0;
	bBegun = false;
	Failure.Reset();
}

EVBPlayBotState FVBPlayBotSequence::Tick(FVBPlayBotContext& Ctx)
{
	while (Steps.IsValidIndex(Index))
	{
		FVBPlayBotSkill& Step = *Steps[Index];
		if (!bBegun)
		{
			bBegun = true;
			if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"step\",\"f\":%lld,\"i\":%d,\"name\":\"%s\",\"phase\":\"begin\"}"),
				Ctx.Frame, Index, *Step.GetName().ReplaceCharWithEscapedChar()));
			Step.Begin(Ctx);
		}

		const EVBPlayBotState State = Step.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"step\",\"f\":%lld,\"i\":%d,\"name\":\"%s\",\"phase\":\"%s\",\"why\":\"%s\"}"),
			Ctx.Frame, Index, *Step.GetName().ReplaceCharWithEscapedChar(), State == EVBPlayBotState::Failed ? TEXT("fail") : TEXT("end"),
			*Step.GetFailure().ReplaceCharWithEscapedChar()));
		if (State == EVBPlayBotState::Failed)
		{
			Failure = FString::Printf(TEXT("step %d '%s': %s"), Index, *Step.GetName(), *Step.GetFailure());
			return State;
		}

		++Index;
		bBegun = false;
	}

	return EVBPlayBotState::Succeeded;
}

EVBPlayBotState FVBSkillDo::Tick(FVBPlayBotContext& Ctx)
{
	FString Error;
	return Fn(Ctx, Error) ? EVBPlayBotState::Succeeded : Fail(Error);
}

EVBPlayBotState FVBSkillWait::Tick(FVBPlayBotContext& Ctx)
{
	return Ctx.Frame - StartFrame >= Ctx.SecondsToFrames(Seconds) ? EVBPlayBotState::Succeeded : EVBPlayBotState::Running;
}

EVBPlayBotState FVBSkillWaitUntil::Tick(FVBPlayBotContext& Ctx)
{
	if (Condition(Ctx))
	{
		return EVBPlayBotState::Succeeded;
	}

	if (Ctx.Frame - StartFrame > Ctx.SecondsToFrames(TimeoutSeconds))
	{
		return Fail(FString::Printf(TEXT("condition not met within %.1f s"), TimeoutSeconds));
	}

	return EVBPlayBotState::Running;
}

void FVBSkillPress::Begin(FVBPlayBotContext& Ctx)
{
	BeginError.Reset();
	if (!RawKey.IsValid() && !VBPlayBotSkills::EnsureAction(Ctx, Action, BeginError))
	{
		return;
	}

	PressId = VBPlayBotSkills::ReactPress(Ctx, Action, RawKey, Name, Ctx.Frame, HoldSeconds);
	if (PressId == INDEX_NONE)
	{
		BeginError = TEXT("press could not be scheduled (no key)");
	}
}

EVBPlayBotState FVBSkillPress::Tick(FVBPlayBotContext& Ctx)
{
	if (!BeginError.IsEmpty())
	{
		return Fail(BeginError);
	}
	// 버튼이 눌렸다가 다시 올라오면 끝난다.
	return Ctx.Input->IsActive(PressId) ? EVBPlayBotState::Running : EVBPlayBotState::Succeeded;
}

EVBPlayBotState FVBSkillApproach::Tick(FVBPlayBotContext& Ctx)
{
	AActor* Target = TargetFn(Ctx);
	if (!Target)
	{
		return Fail(TEXT("no target"));
	}

	const bool bFacing = VBPlayBotSkills::TurnCameraToward(Ctx, Target);
	if (Ctx.Distance2D(Target) <= Range && bFacing)
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		Ctx.Input->SetLookWant(FVector2D::ZeroVector);
		return EVBPlayBotState::Succeeded;
	}

	if (Ctx.Distance2D(Target) <= Range)
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		return EVBPlayBotState::Running;
	}

	if (Ctx.Frame - StartFrame > Ctx.SecondsToFrames(TimeoutSeconds))
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		return Fail(FString::Printf(TEXT("still %.0f cm from %s after %.1f s"), Ctx.Distance2D(Target), *Ctx.Label(Target),
			TimeoutSeconds));
	}

	if (Ctx.Frame >= NextCorrection)
	{
		const FVector2D Aim = Ctx.ToStick(Target->GetActorLocation() - Ctx.Player->GetActorLocation())
			.GetRotated(Ctx.Human->SampleDirectionErrorDeg()) * Ctx.Human->GetProfile().MoveMagnitude;
		Ctx.Input->SetStickWant(Aim);
		NextCorrection = Ctx.Frame + FMath::Max<int64>(1, Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs()));
	}

	return EVBPlayBotState::Running;
}

EVBPlayBotState FVBSkillApproachPoint::Tick(FVBPlayBotContext& Ctx)
{
	const FVector Point = PointFn(Ctx);
	const FVector ToPoint = (Point - Ctx.Player->GetActorLocation()) * FVector(1.0, 1.0, 0.0);
	if (ToPoint.Size() <= Tolerance)
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		return EVBPlayBotState::Succeeded;
	}

	if (Ctx.Frame - StartFrame > Ctx.SecondsToFrames(TimeoutSeconds))
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		return Fail(FString::Printf(TEXT("still %.0f cm from the point after %.1f s"), ToPoint.Size(), TimeoutSeconds));
	}

	if (Ctx.Frame >= NextCorrection)
	{
		Ctx.Input->SetStickWant(Ctx.ToStick(ToPoint).GetRotated(Ctx.Human->SampleDirectionErrorDeg()) * Ctx.Human->GetProfile().MoveMagnitude);
		NextCorrection = Ctx.Frame + FMath::Max<int64>(1, Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs()));
	}

	return EVBPlayBotState::Running;
}

EVBPlayBotState FVBSkillMove::Tick(FVBPlayBotContext& Ctx)
{
	if (Ctx.Frame - StartFrame >= Ctx.SecondsToFrames(Seconds))
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		return EVBPlayBotState::Succeeded;
	}

	if (Ctx.Frame >= NextCorrection)
	{
		const FVector2D Aim = Ctx.ToStick(DirectionFn(Ctx)).GetRotated(Ctx.Human->SampleDirectionErrorDeg()) * Ctx.Human->GetProfile().MoveMagnitude;
		Ctx.Input->SetStickWant(Aim);
		NextCorrection = Ctx.Frame + FMath::Max<int64>(1, Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs()));
	}

	return EVBPlayBotState::Running;
}

void FVBSkillLook::Begin(FVBPlayBotContext& Ctx)
{
	StartFrame = Ctx.Frame;
	Ctx.Input->SetLookWant(Stick);
}

EVBPlayBotState FVBSkillLook::Tick(FVBPlayBotContext& Ctx)
{
	if (Ctx.Frame - StartFrame < Ctx.SecondsToFrames(Seconds))
	{
		return EVBPlayBotState::Running;
	}

	Ctx.Input->SetLookWant(FVector2D::ZeroVector);
	return EVBPlayBotState::Succeeded;
}

EVBPlayBotState FVBSkillMenuSteer::Tick(FVBPlayBotContext& Ctx)
{
	if (PressId != INDEX_NONE && Ctx.Input->IsActive(PressId))
	{
		return EVBPlayBotState::Running;
	}

	if (PressId != INDEX_NONE)
	{
		PressId = INDEX_NONE;
		ReadyFrame = Ctx.Frame + 2;
	}

	if (Ctx.Frame < ReadyFrame)
	{
		return EVBPlayBotState::Running;
	}

	const TArray<FString> Texts = VBPlayBotSkills::FocusTexts();
	for (const FString& Text : Texts)
	{
		if (Labels.ContainsByPredicate([&Text](const FString& L) { return Text.Contains(L); }))
		{
			if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"menu_found\",\"f\":%lld,\"label\":\"%s\",\"text\":\"%s\",\"steps\":%d}"),
				Ctx.Frame, *FString::Join(Labels, TEXT("|")).ReplaceCharWithEscapedChar(), *Text.ReplaceCharWithEscapedChar(), Steps));
			return EVBPlayBotState::Succeeded;
		}
	}

	if (Steps >= MaxSteps)
	{
		return Fail(FString::Printf(TEXT("'%s' not highlighted after %d steered presses; focus shows [%s]"),
			*FString::Join(Labels, TEXT("|")), Steps, *FString::Join(Texts, TEXT(" / "))));
	}
	// 목표와 현재 강조가 화면 어디에 있는지 찾는다.
	const TSharedPtr<SWindow> Window = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetActiveTopLevelRegularWindow() : nullptr;
	const TSharedPtr<SWidget> Focus = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetUserFocusedWidget(0) : nullptr;
	FVector2D Target;
	if (!Window.IsValid() || !Focus.IsValid() || !VBPlayBotSkills::FindVisibleText(Window.ToSharedRef(), Labels, Target, 0))
	{
		return Fail(FString::Printf(TEXT("'%s' is not visible on screen (or nothing has focus)"), *FString::Join(Labels, TEXT("|"))));
	}

	const FGeometry& FocusGeo = Focus->GetCachedGeometry();
	const FVector2D From = FocusGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
	const FVector2D Delta = Target - From;
	// 목표의 줄에 먼저 간 뒤 줄을 따라 움직인다. 큰 축을 고르면 한 줄의 "<" ">" 버튼 사이에서 오간다.
	const bool bOtherRow = FMath::Abs(Delta.Y) > FocusGeo.GetAbsoluteSize().Y * 0.6;
	const FKey Key = bOtherRow
		? (Delta.Y > 0 ? EKeys::Gamepad_DPad_Down : EKeys::Gamepad_DPad_Up)
		: (Delta.X > 0 ? EKeys::Gamepad_DPad_Right : EKeys::Gamepad_DPad_Left);
	++Steps;
	PressId = VBPlayBotSkills::ReactPress(Ctx, NAME_None, Key, Name, Ctx.Frame, 0.0);
	return PressId == INDEX_NONE ? Fail(TEXT("steer press not scheduled")) : EVBPlayBotState::Running;
}

void FVBSkillMenuClick::Begin(FVBPlayBotContext& Ctx)
{
	StartFrame = Ctx.Frame;
	ReadyFrame = Ctx.Frame;
	Clicks = 0;
	MoveStart = DownFrame = UpFrame = -1;
}

bool FVBSkillMenuClick::Locate(FVector2D& OutCentre, TArray<FString>* OutRow) const
{
	const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
	if (!Window.IsValid())
	{
		return false;
	}

	for (const FString& Label : Labels)
	{
		if (VBPlayBotSkills::FindRowTarget(Window.ToSharedRef(), Label, OutCentre, OutRow))
		{
			return true;
		}
	}

	return false;
}

void FVBSkillMenuClick::SendMove(const FVector2D& To)
{
	FSlateApplication& Slate = FSlateApplication::Get();
	const FPointerEvent Move(0, FSlateApplication::CursorPointerIndex, To, Last, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
	Slate.ProcessMouseMoveEvent(Move);
	Last = To;
}

EVBPlayBotState FVBSkillMenuClick::Tick(FVBPlayBotContext& Ctx)
{
	if (!FSlateApplication::IsInitialized())
	{
		return Fail(TEXT("Slate unavailable"));
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	if (MoveStart < 0)
	{
		if (Ctx.Frame < ReadyFrame)
		{
			return EVBPlayBotState::Running;
		}

		TArray<FString> Row;
		const bool bFound = Locate(Target, &Row);
		if (bFound && !Until.IsEmpty() && Row.ContainsByPredicate([this](const FString& T) { return T.Contains(Until); }))
		{
			if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"menu_found\",\"f\":%lld,\"label\":\"%s\",\"text\":\"%s\",\"steps\":%d}"),
				Ctx.Frame, *Until.ReplaceCharWithEscapedChar(), *FString::Join(Row, TEXT(" / ")).ReplaceCharWithEscapedChar(), Clicks));
			return EVBPlayBotState::Succeeded;
		}

		if (!Until.IsEmpty() && Clicks >= MaxClicks)
		{
			return Fail(FString::Printf(TEXT("row never showed '%s' after %d clicks; row shows [%s]"), *Until, Clicks,
				*FString::Join(Row, TEXT(" / "))));
		}

		if (!bFound)
		{
			if (Ctx.Frame - StartFrame > Ctx.SecondsToFrames(TimeoutSeconds))
			{
				return Fail(FString::Printf(TEXT("'%s' never became visible"), *FString::Join(Labels, TEXT("|"))));
			}

			return EVBPlayBotState::Running;
		}
		// 보였으니 반응한 뒤 움직인다. 이동 시간은 profile 의 보정 간격을 따른다 (Fitts 측정값 없음).
		const TSharedPtr<SWindow> Window = Slate.GetActiveTopLevelRegularWindow();
		From = !Last.IsZero() ? Last : (Window.IsValid() ? FVector2D(Window->GetPositionInScreen() + Window->GetSizeInScreen() * 0.5f) : Target);
		Last = From;
		MoveStart = Ctx.Frame + Ctx.Human->GetProfile().PresentationFrames + Ctx.MsToFrames(Ctx.Human->SampleSimpleReactionMs());
		MoveFrames = FMath::Max<int64>(1, Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs()));
		DownFrame = UpFrame = -1;
		return EVBPlayBotState::Running;
	}

	if (Ctx.Frame < MoveStart)
	{
		return EVBPlayBotState::Running;
	}

	if (DownFrame < 0)
	{
		// 메뉴가 움직이며 들어오므로 이름표 위치를 매 프레임 다시 읽는다. 옛 위치는 버튼을 놓친다.
		Locate(Target, nullptr);
		const double Alpha = FMath::Clamp(static_cast<double>(Ctx.Frame - MoveStart + 1) / MoveFrames, 0.0, 1.0);
		SendMove(FMath::Lerp(From, Target, FMath::InterpEaseInOut(0.0, 1.0, Alpha, 2.0)));
		if (Alpha < 1.0)
		{
			return EVBPlayBotState::Running;
		}
		// 게임 viewport 는 gameplay 의 pointer capture 를 쥐고 있고, 합성 이벤트로는 풀리지 않는다.
		// UI 가 Menu input mode 일 때만 직접 풀고 기록에 남긴다.
		bool bReleasedViewportCapture = false;
		const TSharedPtr<FSlateUser> CaptureUser = Slate.GetUser(0);
		const TSharedPtr<SWidget> Captor = CaptureUser.IsValid() ? CaptureUser->GetCursorCaptor() : nullptr;
		const FString CaptorType = Captor.IsValid() ? Captor->GetTypeAsString() : FString(TEXT("none"));
		const UCommonUIActionRouterBase* Router = Ctx.PC && Ctx.PC->GetLocalPlayer() ? Ctx.PC->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
		const bool bMenuMode = Router && Router->GetActiveInputMode() == ECommonInputMode::Menu;
		if (Captor.IsValid() && CaptorType == TEXT("SViewport") && bMenuMode)
		{
			Slate.ReleaseAllPointerCapture(0);
			bReleasedViewportCapture = true;
		}

		TSet<FKey> Pressed;
		Pressed.Add(EKeys::LeftMouseButton);
		Slate.ProcessMouseButtonDownEvent(nullptr,
			FPointerEvent(0, FSlateApplication::CursorPointerIndex, Target, Target, Pressed, EKeys::LeftMouseButton, 0.f,
			FModifierKeysState()));
		DownFrame = Ctx.Frame;
		UpFrame = Ctx.Frame + FMath::Max<int64>(1, Ctx.MsToFrames(Ctx.Human->SampleHoldMs()));
		// 진단 기록 - 누가 pointer capture 를 쥐었는지, 클릭 지점 아래 어떤 widget 이 있는지 남긴다.
		const TSharedPtr<FSlateUser> User = Slate.GetUser(0);
		const bool bCaptured = User.IsValid() && User->HasCapture(FSlateApplication::CursorPointerIndex);
		const FWidgetPath Under = Slate.LocateWindowUnderMouse(Target, Slate.GetInteractiveTopLevelWindows(), false, 0);
		const FString UnderType = Under.IsValid() ? Under.Widgets.Last().Widget->GetTypeAsString() : FString(TEXT("none"));
		const TSharedPtr<SWindow> Win = Slate.GetActiveTopLevelRegularWindow();
		const FVector2D WinPos = Win.IsValid() ? FVector2D(Win->GetPositionInScreen()) : FVector2D::ZeroVector;
		const FVector2D WinSize = Win.IsValid() ? FVector2D(Win->GetSizeInScreen()) : FVector2D::ZeroVector;
		if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"click\",\"f\":%lld,\"label\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"win\":[%.1f,%.1f,%.1f,%.1f],\"dpi\":%.3f,\"captured\":%s,\"under\":\"%s\",\"releasedViewportCapture\":%s,\"captor\":\"%s\",\"menuMode\":%s}"),
			Ctx.Frame, *FString::Join(Labels,
				TEXT("|")).ReplaceCharWithEscapedChar(), Target.X, Target.Y, WinPos.X, WinPos.Y, WinSize.X, WinSize.Y,
			Win.IsValid() ? Win->GetDPIScaleFactor() : 0.f, bCaptured ? TEXT("true") : TEXT("false"), *UnderType, bReleasedViewportCapture ? TEXT("true") : TEXT("false"), *CaptorType, bMenuMode ? TEXT("true") : TEXT("false")));
		return EVBPlayBotState::Running;
	}

	if (Ctx.Frame < UpFrame)
	{
		return EVBPlayBotState::Running;
	}
	// SButton 은 hover 중에만 눌리고 Slate 는 매 프레임 실제 OS 커서로 move 를 만들어 hover 를 푼다.
	// 그래서 떼기 직전에 우리 위치를 다시 보낸다 (사용자 커서는 건드리지 않는다).
	SendMove(Target);
	const TSharedPtr<FSlateUser> UpUser = Slate.GetUser(0);
	const TSharedPtr<SWidget> UpCaptor = UpUser.IsValid() ? UpUser->GetCursorCaptor() : nullptr;
	const bool bUpHandled = Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, FSlateApplication::CursorPointerIndex, Target, Target,
		TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"click_up\",\"f\":%lld,\"captorAtUp\":\"%s\",\"handled\":%s}"),
		Ctx.Frame, UpCaptor.IsValid() ? *UpCaptor->GetTypeAsString() : TEXT("none"), bUpHandled ? TEXT("true") : TEXT("false")));
	++Clicks;
	if (Until.IsEmpty())
	{
		return EVBPlayBotState::Succeeded;
	}
	// 화면이 줄을 다시 그린 뒤에 다시 본다.
	MoveStart = -1;
	ReadyFrame = Ctx.Frame + 3;
	return EVBPlayBotState::Running;
}

void FVBSkillPressResolved::Begin(FVBPlayBotContext& Ctx)
{
	BeginError.Reset();
	FKey Key;
	if (!Resolve(Ctx, Key, BeginError))
	{
		return;
	}

	PressId = VBPlayBotSkills::ReactPress(Ctx, NAME_None, Key, Name, Ctx.Frame, 0.0);
	if (PressId == INDEX_NONE)
	{
		BeginError = TEXT("press could not be scheduled (no key)");
	}
}

EVBPlayBotState FVBSkillPressResolved::Tick(FVBPlayBotContext& Ctx)
{
	if (!BeginError.IsEmpty())
	{
		return Fail(BeginError);
	}

	return Ctx.Input->IsActive(PressId) ? EVBPlayBotState::Running : EVBPlayBotState::Succeeded;
}

void FVBSkillMenuSeek::Begin(FVBPlayBotContext& Ctx)
{
	Steps = 0;
	PressId = INDEX_NONE;
	ReadyFrame = Ctx.Frame;
	BeginError.Reset();
	if (Resolve)
	{
		Resolve(Ctx, StepKey, BeginError);
	}
}

EVBPlayBotState FVBSkillMenuSeek::Tick(FVBPlayBotContext& Ctx)
{
	if (!BeginError.IsEmpty())
	{
		return Fail(BeginError);
	}
	// 마지막 누름이 올라오고 새 강조가 한 프레임 그려진 뒤에만 화면을 읽는다.
	if (PressId != INDEX_NONE && Ctx.Input->IsActive(PressId))
	{
		return EVBPlayBotState::Running;
	}

	if (PressId != INDEX_NONE)
	{
		PressId = INDEX_NONE;
		ReadyFrame = Ctx.Frame + 2;
	}

	if (Ctx.Frame < ReadyFrame)
	{
		return EVBPlayBotState::Running;
	}

	const TArray<FString> Texts = VBPlayBotSkills::FocusTexts();
	for (const FString& Text : Texts)
	{
		for (const FString& Label : Labels)
		{
			if (Text.Contains(Label))
			{
				if (Ctx.Emit) Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"menu_found\",\"f\":%lld,\"label\":\"%s\",\"text\":\"%s\",\"steps\":%d}"),
					Ctx.Frame, *Label.ReplaceCharWithEscapedChar(), *Text.ReplaceCharWithEscapedChar(), Steps));
				return EVBPlayBotState::Succeeded;
			}
		}
	}

	if (Steps >= MaxSteps)
	{
		return Fail(FString::Printf(TEXT("'%s' not highlighted after %d presses of %s; focus shows [%s]"),
			*FString::Join(Labels, TEXT("|")), Steps, *StepKey.ToString(), *FString::Join(Texts, TEXT(" / "))));
	}

	++Steps;
	PressId = VBPlayBotSkills::ReactPress(Ctx, NAME_None, StepKey, Name, Ctx.Frame, 0.0);
	return PressId == INDEX_NONE ? Fail(TEXT("menu step press not scheduled")) : EVBPlayBotState::Running;
}

void FVBSkillAttackUntil::Begin(FVBPlayBotContext& Ctx)
{
	StartFrame = Ctx.Frame;
	NextCorrection = Ctx.Frame;
	AActor* Target = TargetFn(Ctx);
	BaseHits = Target ? Ctx.HitsByPlayer.FindRef(Target) : 0;
	SeenHits = 0;
	PressId = INDEX_NONE;
	LastPressFrame = -1;
}

EVBPlayBotState FVBSkillAttackUntil::Tick(FVBPlayBotContext& Ctx)
{
	AActor* Target = TargetFn(Ctx);
	if (!Target)
	{
		return Fail(TEXT("no target"));
	}

	const int32 Hits = Ctx.HitsByPlayer.FindRef(Target) - BaseHits;
	if (Done(Ctx, Hits) || Ctx.Frame - StartFrame > Ctx.SecondsToFrames(TimeoutSeconds))
	{
		// 다음 단계에 남기지 않는다 - 대기 중인 누름은 버리고, 누르고 있는 것은 끝까지 둔다.
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		Ctx.Input->SetLookWant(FVector2D::ZeroVector);
		Ctx.Input->CancelPending(TEXT("step_done"));
		if (PressId != INDEX_NONE && Ctx.Input->IsActive(PressId))
		{
			return EVBPlayBotState::Running;
		}

		if (Done(Ctx, Hits))
		{
			return EVBPlayBotState::Succeeded;
		}

		return Fail(FString::Printf(TEXT("condition not reached after %d hits in %.1f s"), Hits, TimeoutSeconds));
	}
	// 닿지 않으면 사람처럼 다시 겨누며 걸어 들어가고, 가까워질 때까지 공격하지 않는다.
	if (Ctx.Distance2D(Target) > Range)
	{
		VBPlayBotSkills::TurnCameraToward(Ctx, Target);
		if (Ctx.Frame >= NextCorrection)
		{
			Ctx.Input->SetStickWant(Ctx.ToStick(Target->GetActorLocation() - Ctx.Player->GetActorLocation())
				.GetRotated(Ctx.Human->SampleDirectionErrorDeg()) * Ctx.Human->GetProfile().MoveMagnitude);
			NextCorrection = Ctx.Frame + FMath::Max<int64>(1, Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs()));
		}

		return EVBPlayBotState::Running;
	}

	Ctx.Input->SetStickWant(FVector2D::ZeroVector);
	if (PressId != INDEX_NONE && Ctx.Input->IsActive(PressId))
	{
		return EVBPlayBotState::Running;
	}

	PressId = INDEX_NONE;
	// 먼저 겨눈다 - 공격은 카메라가 보는 쪽으로 나가므로 적이 카메라 뒤에 있으면 허공을 친다.
	if (!VBPlayBotSkills::TurnCameraToward(Ctx, Target))
	{
		return EVBPlayBotState::Running;
	}

	FString Error;
	if (!VBPlayBotSkills::EnsureAction(Ctx, Attack, Error))
	{
		return Fail(Error);
	}

	if (Hits > SeenHits)
	{
		// 새 적중이 보이면 그것에 반응한 뒤 다시 누른다.
		SeenHits = Hits;
		PressId = VBPlayBotSkills::ReactPress(Ctx, Attack, FKey(), Name + TEXT(" after contact"), Ctx.LastPlayerHitFrame, 0.0);
		LastPressFrame = Ctx.Frame;
	}
	else if (MashSeconds > 0.0 && LastPressFrame >= 0 && Ctx.Frame - LastPressFrame >= Ctx.SecondsToFrames(MashSeconds))
	{
		// 일부러 잇는다 - 앞 휘두름이 맞는 걸 보지 않고 박자에 맞춰 다음을 누른다.
		PressId = VBPlayBotSkills::ReactPress(Ctx, Attack, FKey(), Name + TEXT(" chain"), Ctx.Frame, 0.0);
		LastPressFrame = Ctx.Frame;
	}
	else if (LastPressFrame < 0 || Ctx.Frame - LastPressFrame > Ctx.SecondsToFrames(1.2))
	{
		// 한동안 적중이 없으면 (첫 휘두름이거나 빗나감) 지금 시점에 반응한 뒤 다시 누른다.
		PressId = VBPlayBotSkills::ReactPress(Ctx, Attack, FKey(), Name + TEXT(" retry"), Ctx.Frame, 0.0);
		LastPressFrame = Ctx.Frame;
	}

	return EVBPlayBotState::Running;
}

EVBPlayBotState FVBSkillPadMode::Tick(FVBPlayBotContext& Ctx)
{
	const UCommonInputSubsystem* CommonInput = Ctx.PC ? UCommonInputSubsystem::Get(Ctx.PC->GetLocalPlayer()) : nullptr;
	if (!CommonInput)
	{
		return Fail(TEXT("no CommonInput subsystem"));
	}

	if (CommonInput->GetCurrentInputType() != ECommonInputType::Gamepad)
	{
		ReadyFrame = -1;
		if (Ctx.Frame - StartFrame > Ctx.SecondsToFrames(TimeoutSeconds))
		{
			return Fail(TEXT("pad input never came back (CommonInput not Gamepad)"));
		}

		if ((Ctx.Frame - StartFrame) % 10 == 0)
		{
			Ctx.Input->SendWakeTap();
		}

		return EVBPlayBotState::Running;
	}
	// 입력 장치가 바뀌면 mapping context 가 교체되므로 실행 시작 때처럼 몇 프레임 기다린다.
	if (ReadyFrame < 0)
	{
		ReadyFrame = Ctx.Frame + 3;
	}

	return Ctx.Frame >= ReadyFrame ? EVBPlayBotState::Succeeded : EVBPlayBotState::Running;
}

EVBPlayBotState FVBSkillEquip::Tick(FVBPlayBotContext& Ctx)
{
	const AVBCharacter* Character = Cast<AVBCharacter>(Ctx.Player);
	const UVBWeaponStateComponent* State = UVBWeaponStateComponent::FindFor(Character);
	const int64 Wanted = StaticEnum<EVBWeaponType>()->GetValueByNameString(Weapon);
	if (!State || Wanted == INDEX_NONE)
	{
		return Fail(FString::Printf(TEXT("cannot equip '%s' (no weapon state or unknown weapon name)"), *Weapon));
	}

	if (State->GetCurrentWeaponType() == static_cast<EVBWeaponType>(Wanted) && State->IsWeaponSummoned())
	{
		return EVBPlayBotState::Succeeded;
	}

	if (!bPressed)
	{
		// 슬롯 버튼은 토글이라 이미 꺼낸 무기면 집어넣는다. 꺼내지 않았을 때만 누른다.
		bPressed = true;
		FString Error;
		if (!VBPlayBotSkills::EnsureAction(Ctx, SlotAction, Error))
		{
			return Fail(Error);
		}

		if (VBPlayBotSkills::ReactPress(Ctx, SlotAction, FKey(), Name, Ctx.Frame, 0.0) == INDEX_NONE)
		{
			return Fail(TEXT("slot press not scheduled"));
		}
	}

	if (Ctx.Frame - StartFrame > Ctx.SecondsToFrames(4.0))
	{
		return Fail(FString::Printf(TEXT("%s was not summoned after the slot button"), *Weapon));
	}

	return EVBPlayBotState::Running;
}
